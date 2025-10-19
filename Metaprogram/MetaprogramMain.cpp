#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "TypeInfo.h"
#include "MetaprogramCommon.h"


struct HlslFileData {
	HashTable<String, u32> includes;
	StringBuilder builder;
};

struct RootSignaturePassData {
	ArrayView<u32> root_signature_stream;
	String include_file_name;
	String render_pass_name;
};

struct RootSignatureFileData {
	StringBuilder builder;
	Array<RootSignaturePassData> root_signatures;
};

struct ShaderDefinitionFileData {
	StringBuilder builder;
	Array<String> shader_names;
};

static void GatherIncludesForDependentTypes(StackAllocator* alloc, HashTable<String, u32>& includes, TypeInfoStruct* type_info) {
	if (type_info == nullptr) return;
	
	for (auto& field : type_info->fields) {
		if (auto* note = FindNote<Meta::HlslFile>(field.type)) {
			HashTableAddOrFind(includes, alloc, note->filename, 0u);
		}
		
		auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(field.type);
		if (type_info_struct == nullptr) continue;
		
		for (auto& field : type_info_struct->fields) {
			if (HasAnyFlags(field.flags, TypeInfoStructFieldFlags::TemplateParameter) && field.type == &type_info_type) {
				auto* template_type = (TypeInfo*)field.constant_value;
				
				if (auto* note = FindNote<Meta::HlslFile>(template_type)) {
					HashTableAddOrFind(includes, alloc, note->filename, 0u);
				}
				
				GatherIncludesForDependentTypes(alloc, includes, TypeInfoCast<TypeInfoStruct>(template_type));
			}
		}
	}
}

static void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfo* type_info) {
	auto& builder = hlsl_file.builder;
	
	auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	GatherIncludesForDependentTypes(alloc, hlsl_file.includes, type_info_struct);
	
	builder.Append("struct %.*s {\n", (s32)name.count, name.data);
	builder.Indent();
	
	u32 constant_count = 0;
	for (auto& field : type_info_struct->fields) {
		DebugAssert(field.type != nullptr, "Type of field '%.*s' in struct '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
		
		if (field.type == &type_info_type) {
			builder.Append("\n");
			GenerateCodeForHlslFile(alloc, hlsl_file, (TypeInfo*)field.constant_value);
		} else if (field.constant_value) {
			auto type_name = PrintTypeName(field.type);
			builder.Append("compile_const %.*s %.*s;\n", (s32)type_name.count, type_name.data, (s32)field.name.count, field.name.data);
			constant_count += 1;
		} else {
			auto type_name = PrintTypeName(field.type);
			builder.Append("%.*s %.*s;\n", (s32)type_name.count, type_name.data, (s32)field.name.count, field.name.data);
		}
	}
	
	builder.Unindent();
	builder.AppendUnformatted("};\n\n"_sl);
	
	// Hlsl doesn't support inline constant declarations for non trivial types. Output them after the struct.
	if (constant_count != 0) {
		for (auto& field : type_info_struct->fields) {
			if (field.constant_value == nullptr) continue;
			
			auto type_name = PrintTypeName(field.type);
			auto type_value = PrintTypeValue(alloc, field.type, field.constant_value);
			builder.Append("compile_const %.*s %.*s::%.*s = %.*s;\n", (s32)type_name.count, type_name.data, (s32)name.count, name.data, (s32)field.name.count, field.name.data, (s32)type_value.count, type_value.data);
		}
		builder.AppendUnformatted("\n"_sl);
	}
}

static HlslFileData& AddOrFindHlslFile(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files, String filename) {
	auto [element, is_added] = HashTableAddOrFind(hlsl_files, alloc, filename, HlslFileData{});
	auto& hlsl_file = element->value;
	auto& builder = hlsl_file.builder;
	
	if (is_added) builder.alloc = alloc;
	
	return hlsl_file;
}

static void GenerateCodeForHlslFile(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files, TypeInfo* type_info, Meta::HlslFile* hlsl_file_note) {
	auto& hlsl_file = AddOrFindHlslFile(alloc, hlsl_files, hlsl_file_note->filename);
	
	GenerateCodeForHlslFile(alloc, hlsl_file, type_info);
}

static void WriteHlslFilesToDisk(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files) {
	for (auto& [filename, hlsl_file] : hlsl_files) {
		auto include_guard = StringFormat(alloc, "GENERATED_%.*s", (s32)filename.count, filename.data);
		for (u64 i = 0; i < include_guard.count; i += 1) {
			auto c = include_guard[i];
			
			if (c == '.') {
				include_guard[i] = '_';
			} else {
				include_guard[i] = CharToUpperCase(c);
			}
		}
		
		StringBuilder builder;
		builder.alloc = alloc;
		builder.Append("#ifndef %.*s\n", (s32)include_guard.count, include_guard.data);
		builder.Append("#define %.*s\n", (s32)include_guard.count, include_guard.data);
		builder.AppendUnformatted("#include \"Basic.hlsl\"\n"_sl);
		
		for (auto& [include, dummy] : hlsl_file.includes) {
			if (include == filename) continue;
			builder.Append("#include \"%.*s\"\n", (s32)include.count, include.data);
		}
		builder.AppendUnformatted("\n"_sl);
		
		builder.AppendBuilder(hlsl_file.builder);
		
		builder.Append("#endif // %.*s\n", (s32)include_guard.count, include_guard.data);
		
		auto output_filepath = StringFormat(alloc, "Shaders/Generated/%.*s", (s32)filename.count, filename.data);
		WriteGeneratedFile(alloc, output_filepath, builder.ToString());
	}
}

static void GenerateCodeForRenderPass(StackAllocator* alloc, String filename, HlslFileData& hlsl_bindings_file, RootSignatureFileData& root_signature_file, TypeInfo* type_info, Meta::RenderPass* render_pass_note) {
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	TypeInfoStruct* root_signature_type = nullptr;
	for (auto& field : type_info_struct->fields) {
		if (field.name == "RootSignature"_sl) {
			root_signature_type = (TypeInfoStruct*)field.constant_value;
			break;
		}
	}
	DebugAssert(root_signature_type != nullptr, "RenderPass '%.*s' is missing root signature.", (s32)name.count, name.data);
	
	// Validate root signature:
	for (auto& field : root_signature_type->fields) {
		if (field.type == &type_info_type) continue;
		
		auto* root_argument_type_note = FindNote<RootArgumentType>(field.type);
		if (root_argument_type_note == nullptr) {
			auto type_name = PrintTypeName(field.type);
			DebugAssertAlways("Unexpected field '%.*s' of type '%.*s' used in a root signature of pass '%.*s'. Only root arguments are allowed.", (s32)field.name.count, field.name.data, (s32)type_name.count, type_name.data, (s32)name.count, name.data);
			continue;
		}
		auto root_argument_type = *root_argument_type_note;
		
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		if (root_argument_type == RootArgumentType::DescriptorTable) {
			DebugAssert(template_type, "Template type of DescriptorTable '%.*s' in render pass '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
			
			for (auto& field : template_type->fields) {
				auto* descriptor_type_note = FindNote<ResourceDescriptorType>(field.type);
				if (descriptor_type_note == nullptr) {
					auto type_name = PrintTypeName(field.type);
					DebugAssertAlways("Unexpected field '%.*s' of type '%.*s' used in a descriptor table of pass '%.*s'. Only descriptors are allowed.", (s32)field.name.count, field.name.data, (s32)type_name.count, type_name.data, (s32)name.count, name.data);
				}
			}
		} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
			DebugAssert(template_type, "Template type of ConstantBuffer '%.*s' in render pass '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
		} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
			DebugAssert(template_type, "Template type of PushConstantBuffer '%.*s' in render pass '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
		}
	}
	
	
	// Build root signature stream:
	u32 root_parameter_count = 0;
	Array<u32> root_signature_stream;
	for (auto& field : root_signature_type->fields) {
		if (field.type == &type_info_type) continue;
		
		auto root_argument_type = *FindNote<RootArgumentType>(field.type);
		ArrayAppend(root_signature_stream, alloc, (u32)root_argument_type);
		
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		if (root_argument_type == RootArgumentType::DescriptorTable) {
			ArrayAppend(root_signature_stream, alloc, (u32)template_type->fields.count);
			
			for (auto& field : template_type->fields) {
				auto descriptor_type = *FindNote<ResourceDescriptorType>(field.type);
				ArrayAppend(root_signature_stream, alloc, (u32)descriptor_type);
			}
			root_parameter_count += 1;
		} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
			root_parameter_count += 1;
		} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
			ArrayAppend(root_signature_stream, alloc, DivideAndRoundUp(ComputeTypeSize(template_type), sizeof(u32)));
			root_parameter_count += 1;
		}
	}
	
	{
		auto& cpp_builder = root_signature_file.builder;
		
		cpp_builder.Append("%.*s::RootSignature %.*s::root_signature = {\n", (s32)name.count, name.data, (s32)name.count, name.data);
		cpp_builder.Indent();
		
		auto pass_type = PrintTypeValue(alloc, TypeInfoOf<CommandQueueType>(), &render_pass_note->pass_type);
		cpp_builder.Append("RootSignatureID{ %u }, %u, %.*s,\n", (u32)root_signature_file.root_signatures.count, root_parameter_count, (s32)pass_type.count, pass_type.data);
		
		u32 root_parameter_index = 0;
		for (auto& field : root_signature_type->fields) {
			if (field.type == &type_info_type) continue;
			
			auto root_argument_type = *FindNote<RootArgumentType>(field.type);
			
			auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
			if (root_argument_type == RootArgumentType::DescriptorTable) {
				cpp_builder.Append("{ %u, %u },\n", root_parameter_index, (u32)template_type->fields.count);
			} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
				cpp_builder.Append("{ %u },\n", root_parameter_index);
			} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
				cpp_builder.Append("{ %u },\n", root_parameter_index);
			}
			root_parameter_index += 1;
		}
		
		cpp_builder.Unindent();
		cpp_builder.Append("};\n");
		
		cpp_builder.Append("static u32 root_signature_stream_%.*s[] = { ", (s32)name.count, name.data, (s32)name.count, name.data);
		for (u32 dword : root_signature_stream) {
			cpp_builder.Append("%u, ", dword);
		}
		cpp_builder.Append("};\n\n");
	}
	
	GatherIncludesForDependentTypes(alloc, hlsl_bindings_file.includes, root_signature_type);
	
	u32 cbv_index = 0;
	u32 srv_index = 0;
	u32 uav_index = 0;
	auto& builder = hlsl_bindings_file.builder;
	for (auto& field : root_signature_type->fields) {
		if (field.type == &type_info_type) continue;
		
		auto root_argument_type = *FindNote<RootArgumentType>(field.type);
		
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		if (root_argument_type == RootArgumentType::DescriptorTable) {
			compile_const char* hlsl_descriptor_type_names[] = {
				"None",
				"Texture2D",
				"RWTexture2D",
				"StructuredBuffer",
				"RWStructuredBuffer",
				"ByteAddressBuffer",
				"RWByteAddressBuffer",
			};
			
			for (auto& field : template_type->fields) {
				auto descriptor_type = *FindNote<ResourceDescriptorType>(field.type);
				u32 descriptor_type_index = (u32)descriptor_type >> (u32)ResourceDescriptorType::IndexOffset;
				const char* descriptor_name = hlsl_descriptor_type_names[descriptor_type_index];
				
				bool is_srv = HasAnyFlags(descriptor_type, ResourceDescriptorType::AnySRV);
				u32 register_index = is_srv ? srv_index++ : uav_index++;
				char register_type = is_srv ? 't' : 'u';
				
				auto* template_type = ExtractTemplateParameterType(field.type, 0);
				if (template_type != nullptr) {
					auto template_type_name = PrintTypeName(template_type);
					builder.Append("%s<%.*s> %.*s : register(%c%u);\n", descriptor_name, (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, register_type, register_index);
				} else {
					builder.Append("%s %.*s : register(%c%u);\n", descriptor_name, (s32)field.name.count, field.name.data, register_type, register_index);
				}
			}
		} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
			auto template_type_name = PrintTypeName(template_type);
			builder.Append("ConstantBuffer<%.*s> %.*s : register(b%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, cbv_index);
			cbv_index += 1;
		} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
			auto template_type_name = PrintTypeName(template_type);
			builder.Append("ConstantBuffer<%.*s> %.*s : register(b%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, cbv_index);
			cbv_index += 1;
		}
	}
	builder.AppendUnformatted("\n"_sl);
	
	
	RootSignaturePassData root_signature;
	root_signature.root_signature_stream = root_signature_stream;
	root_signature.include_file_name = filename;
	root_signature.render_pass_name  = name;
	ArrayAppend(root_signature_file.root_signatures, alloc, root_signature);
}

static void GenerateCodeForRenderPass(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_bindings_files, RootSignatureFileData& root_signature_file, TypeInfo* type_info, Meta::RenderPass* render_pass_note) {
	auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
	auto render_pass_name = ExtractNameWithoutNamespace(type_info_struct->name);
	auto filename = StringFormat(alloc, "%.*s.hlsl", (s32)render_pass_name.count, render_pass_name.data);
	
	auto& hlsl_file = AddOrFindHlslFile(alloc, hlsl_bindings_files, filename);
	
	GenerateCodeForRenderPass(alloc, filename, hlsl_file, root_signature_file, type_info, render_pass_note);
}

static void GenerateCodeForShaderDefinition(StackAllocator* alloc, ShaderDefinitionFileData& shader_definition_file, TypeInfo* type_info, Meta::ShaderName* note) {
	auto* type_info_enum = TypeInfoCast<TypeInfoEnum>(type_info);
	auto name = type_info_enum->name;
	
	auto& builder = shader_definition_file.builder;
	u32 define_count = 0;
	if (type_info_enum->fields.count != 0) {
		builder.Append("static String shader_defines_%.*s[] = {\n", (s32)name.count, name.data);
		builder.Indent();
		
		for (auto& field : type_info_enum->fields) {
			if (CountSetBits(field.value) != 1) continue;
			
			DebugAssert(FirstBitLow(field.value) == define_count, "Out of order shader definition '%.*s' in shader '%.*s'. Position in enum: '%u', Value: '1u << %u'.", (s32)field.name.count, field.name.data, (s32)name.count, name.data, define_count, FirstBitLow(field.value));
			
			u64 underscore_count = 0;
			for (u64 i = 0; i < field.name.count - 1; i += 1) {
				char c0 = field.name[i + 0];
				char c1 = field.name[i + 1];
				
				if (CharIsLowerCase(c0) && CharIsUpperCase(c1)) {
					underscore_count += 1;
				}
			}
			
			auto string = StringAllocate(alloc, field.name.count + underscore_count);
			
			u64 offset = 0;
			for (u64 i = 0; i < field.name.count; i += 1) {
				char c0 = field.name[i + 0];
				string[offset++] = CharToUpperCase(c0);
				
				if (i + 1 < field.name.count && CharIsLowerCase(c0) && CharIsUpperCase(field.name[i + 1])) {
					string[offset++] = '_';
				}
			}
			
			builder.Append("\"%.*s\"_sl,\n", (s32)string.count, string.data);
			define_count += 1;
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	{
		builder.Append("static ShaderDefinition shader_definition_%.*s = {\n", (s32)name.count, name.data);
		builder.Indent();
		
		builder.Append("\"%.*s\"_sl,\n", (s32)note->filename.count, note->filename.data);
		if (define_count != 0) {
			builder.Append("ArrayView<String>{ shader_defines_%.*s, %u },\n", (s32)name.count, name.data, define_count);
		} else {
			builder.AppendUnformatted("ArrayView<String>{},\n"_sl);
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	builder.Append("ShaderID %.*sID = { %u };\n\n\n", (s32)name.count, name.data, (u32)shader_definition_file.shader_names.count);
	
	ArrayAppend(shader_definition_file.shader_names, alloc, name);
}

s32 main(s32 argument_count, const char* arguments[]) {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	if ((argument_count >= 2) && (strcmp(arguments[1], "-m") == 0)) {
		extern void GenerateMathLibrary(StackAllocator* alloc);
		TempAllocationScope(&alloc);
		GenerateMathLibrary(&alloc);
	}
	
	HashTable<String, HlslFileData> hlsl_files;
	HashTable<String, HlslFileData> hlsl_bindings_files;
	
	RootSignatureFileData root_signature_file;
	root_signature_file.builder.alloc = &alloc;
	root_signature_file.builder.AppendUnformatted("#include \"Basic/Basic.h\"\n"_sl);
	root_signature_file.builder.AppendUnformatted("#include \"Engine/RenderPasses.h\"\n"_sl);
	root_signature_file.builder.AppendUnformatted("#include \"GraphicsApi/GraphicsApi.h\"\n\n"_sl);
	
	ShaderDefinitionFileData shader_definition_file;
	shader_definition_file.builder.alloc = &alloc;
	shader_definition_file.builder.AppendUnformatted("#include \"Basic/Basic.h\"\n"_sl);
	shader_definition_file.builder.AppendUnformatted("#include \"Basic/BasicString.h\"\n"_sl);
	shader_definition_file.builder.AppendUnformatted("#include \"Engine/RenderPasses.h\"\n\n"_sl);
	
	extern ArrayView<TypeInfo*> type_table;
	for (auto* type_info : type_table) {
		if (auto* note = FindNote<Meta::HlslFile>(type_info)) {
			GenerateCodeForHlslFile(&alloc, hlsl_files, type_info, note);
		}
		
		if (auto* note = FindNote<Meta::RenderPass>(type_info)) {
			GenerateCodeForRenderPass(&alloc, hlsl_bindings_files, root_signature_file, type_info, note);
		}
		
		if (auto* note = FindNote<Meta::ShaderName>(type_info)) {
			GenerateCodeForShaderDefinition(&alloc, shader_definition_file, type_info, note);
		}
	}
	
	EnsureDirectoryExists(&alloc, "Shaders/Generated/"_sl);
	EnsureDirectoryExists(&alloc, "Engine/Generated/"_sl);
	
	WriteHlslFilesToDisk(&alloc, hlsl_files);
	WriteHlslFilesToDisk(&alloc, hlsl_bindings_files);
	
	{
		auto& builder = root_signature_file.builder;
		{
			builder.AppendUnformatted("static String root_signature_filenames_internal[] = {\n"_sl);
			builder.Indent();
			
			for (auto& root_signature : root_signature_file.root_signatures) {
				builder.Append("\"%.*s\"_sl,\n", (s32)root_signature.include_file_name.count, root_signature.include_file_name.data);
			}
			
			builder.Unindent();
			builder.AppendUnformatted("};\n\n"_sl);
			
			builder.Append("ArrayView<String> root_signature_filenames = { root_signature_filenames_internal, %u };\n\n", (u32)root_signature_file.root_signatures.count);
		}
		
		{
			builder.AppendUnformatted("static ArrayView<u32> root_signature_streams_internal[] = {\n"_sl);
			builder.Indent();
			
			for (auto& root_signature : root_signature_file.root_signatures) {
				builder.Append("{ root_signature_stream_%.*s, %u },\n", (s32)root_signature.render_pass_name.count, root_signature.render_pass_name.data, (u32)root_signature.root_signature_stream.count);
			}
			
			builder.Unindent();
			builder.AppendUnformatted("};\n\n"_sl);
			
			builder.Append("ArrayView<ArrayView<u32>> root_signature_streams = { root_signature_streams_internal, %u };\n\n", (u32)root_signature_file.root_signatures.count);
		}
		
		{
			builder.AppendUnformatted("Array<PipelineDefinition> GatherPipelineDefinitions(StackAllocator* alloc) {\n"_sl);
			builder.Indent();
			
			builder.AppendUnformatted("PipelineLibrary lib;\n"_sl);
			builder.AppendUnformatted("lib.alloc = alloc;\n\n"_sl);
			
			for (auto& root_signature : root_signature_file.root_signatures) {
				auto name = root_signature.render_pass_name;
				
				builder.Append("lib.current_pass_root_signature_id = %.*s::root_signature.root_signature_id;\n", (s32)name.count, name.data);
				builder.Append("%.*s::CreatePipelines(&lib);\n\n", (s32)name.count, name.data);
			}
			
			builder.AppendUnformatted("return lib.pipeline_definitions;\n"_sl);
			
			builder.Unindent();
			builder.AppendUnformatted("};\n\n"_sl);
		}
		
		WriteGeneratedFile(&alloc, "Engine/Generated/RootSignature.cpp"_sl, builder.ToString());
	}
	
	{
		auto& builder = shader_definition_file.builder;
		builder.AppendUnformatted("static ShaderDefinition* shader_definitions[] = {\n"_sl);
		builder.Indent();
		
		for (auto& shader_name : shader_definition_file.shader_names) {
			builder.Append("&shader_definition_%.*s,\n", (s32)shader_name.count, shader_name.data);
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
		
		builder.Append("ArrayView<ShaderDefinition*> shader_definition_table = { shader_definitions, %u };\n\n", (u32)shader_definition_file.shader_names.count);
		
		WriteGeneratedFile(&alloc, "Engine/Generated/ShaderDefinitions.cpp"_sl, builder.ToString());
	}
	
	return 0;
}

