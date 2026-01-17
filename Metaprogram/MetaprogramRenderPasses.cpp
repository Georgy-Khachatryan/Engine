#include "Basic/Basic.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "MetaprogramSystems.h"
#include "MetaprogramCommon.h"


struct HlslFileData {
	StringBuilder builder;
	HashTable<String, void> includes;
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


static void GatherIncludesForDependentTypes(StackAllocator* alloc, HashTable<String, void>& includes, TypeInfoStruct* type_info) {
	if (type_info == nullptr) return;
	
	for (auto& field : type_info->fields) {
		if (auto* note = FindNote<Meta::HlslFile>(field.type)) {
			HashTableAddOrFind(includes, alloc, note->filename);
		}
		
		auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(field.type);
		if (type_info_struct == nullptr) continue;
		
		for (auto& field : type_info_struct->fields) {
			if (HasAnyFlags(field.flags, TypeInfoStructFieldFlags::TemplateParameter) && field.type == &type_info_type) {
				auto* template_type = (TypeInfo*)field.constant_value;
				
				if (auto* note = FindNote<Meta::HlslFile>(template_type)) {
					HashTableAddOrFind(includes, alloc, note->filename);
				}
				
				GatherIncludesForDependentTypes(alloc, includes, TypeInfoCast<TypeInfoStruct>(template_type));
			}
		}
	}
}

static void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfo* type_info);

static void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfoStruct* type_info_struct) {
	auto& builder = hlsl_file.builder;
	
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	GatherIncludesForDependentTypes(alloc, hlsl_file.includes, type_info_struct);
	
	builder.Append("struct % {\n"_sl, name);
	builder.Indent();
	
	u32 constant_count = 0;
	for (auto& field : type_info_struct->fields) {
		CheckFieldIsReflected(alloc, type_info_struct, field);
		
		if (field.type == &type_info_type) {
			auto* field_type = TypeInfoCast<TypeInfoStruct>((TypeInfo*)field.constant_value);
			if (field_type != nullptr) {
				builder.Append("\n"_sl);
				GenerateCodeForHlslFile(alloc, hlsl_file, field_type);
			}
		} else if (field.constant_value) {
			auto type_name = PrintTypeName(alloc, field.type);
			builder.Append("compile_const % %;\n"_sl, type_name, field.name);
			constant_count += 1;
		} else {
			auto type_name = PrintTypeName(alloc, field.type);
			builder.Append("% %;\n"_sl, type_name, field.name);
		}
	}
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	// Hlsl doesn't support inline constant declarations for non trivial types. Output them after the struct.
	if (constant_count != 0) {
		for (auto& field : type_info_struct->fields) {
			if (field.type == &type_info_type || field.constant_value == nullptr) continue;
			
			auto type_name  = PrintTypeName(alloc, field.type);
			auto type_value = PrintTypeValue(alloc, field.type, field.constant_value);
			builder.Append("compile_const % %::% = %;\n"_sl, type_name, name, field.name, type_value);
		}
		builder.Append("\n"_sl);
	}
}

static void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfoEnum* type_info_enum) {
	auto& builder = hlsl_file.builder;
	
	auto name = ExtractNameWithoutNamespace(type_info_enum->name);
	
	builder.Append("enum struct % : % {\n"_sl, name, PrintTypeName(alloc, type_info_enum->underlying_type));
	builder.Indent();
	
	u32 constant_count = 0;
	for (auto& field : type_info_enum->fields) {
		builder.Append("% = %,\n"_sl, field.name, field.value);
	}
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
}

static void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfo* type_info) {
	if (type_info->info_type == TypeInfoType::Struct) {
		GenerateCodeForHlslFile(alloc, hlsl_file, (TypeInfoStruct*)type_info);
	} else if (type_info->info_type == TypeInfoType::Enum) {
		GenerateCodeForHlslFile(alloc, hlsl_file, (TypeInfoEnum*)type_info);
	}
}

static HlslFileData& AddOrFindHlslFile(HashTable<String, HlslFileData>& hlsl_files, StackAllocator* alloc, String filename) {
	auto [element, is_added] = HashTableAddOrFind(hlsl_files, alloc, filename, HlslFileData{ { alloc } });
	return element->value;
}

static void WriteHlslFilesToDisk(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files) {
	for (auto& [filename, hlsl_file] : hlsl_files) {
		if (hlsl_file.builder.total_string_size == 0) continue;
		
		auto include_guard = StringFormat(alloc, "GENERATED_%"_sl, filename);
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
		builder.Append("#ifndef %\n"_sl, include_guard);
		builder.Append("#define %\n"_sl, include_guard);
		builder.Append("#include \"Basic.hlsl\"\n"_sl);
		
		for (auto& [include] : hlsl_file.includes) {
			if (include == filename) continue;
			builder.Append("#include \"%\"\n"_sl, include);
		}
		builder.Append("\n"_sl);
		
		builder.AppendBuilder(hlsl_file.builder);
		
		builder.Append("#endif // %\n"_sl, include_guard);
		
		auto output_filepath = StringFormat(alloc, "Shaders/Generated/%"_sl, filename);
		WriteGeneratedFile(alloc, output_filepath, builder.ToString());
	}
}

static void GenerateCodeForRenderPass(StackAllocator* alloc, String filename, String name, HlslFileData& hlsl_bindings_file, RootSignatureFileData& root_signature_file, TypeInfoStruct* type_info_struct, Meta::RenderPass* render_pass_note) {
	TypeInfoStruct* root_signature_type = nullptr;
	for (auto& field : type_info_struct->fields) {
		if (field.name == "RootSignature"_sl) {
			root_signature_type = (TypeInfoStruct*)field.constant_value;
			break;
		}
	}
	if (root_signature_type == nullptr) return;
	
	
	// Validate root signature:
	for (auto& field : root_signature_type->fields) {
		if (field.type == &type_info_type) continue;
		
		auto* root_argument_type_note = FindNote<RootArgumentType>(field.type);
		if (root_argument_type_note == nullptr) {
			auto type_name = PrintTypeName(alloc, field.type);
			ReportError(alloc, field.source_location, "Unexpected field '%' of type '%' used in a root signature of render pass '%'. Only root arguments are allowed."_sl, field.name, type_name, name);
		}
		auto root_argument_type = *root_argument_type_note;
		
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		if (root_argument_type == RootArgumentType::DescriptorTable) {
			if (template_type == nullptr) {
				ReportError(alloc, field.source_location, "Template type of DescriptorTable '%' in render pass '%' is not reflected."_sl, field.name, name);
			}
			
			for (auto& field : template_type->fields) {
				auto* descriptor_type_note = FindNote<ResourceDescriptorType>(field.type);
				if (descriptor_type_note == nullptr) {
					auto type_name = PrintTypeName(alloc, field.type);
					ReportError(alloc, field.source_location, "Unexpected field '%' of type '%' used in a descriptor table of pass '%'. Only descriptors are allowed."_sl, field.name, type_name, name);
				}
			}
		} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
			if (template_type == nullptr) {
				ReportError(alloc, field.source_location, "Template type of ConstantBuffer '%' in render pass '%' is not reflected."_sl, field.name, name);
			}
		} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
			if (template_type == nullptr) {
				ReportError(alloc, field.source_location, "Template type of PushConstantBuffer '%' in render pass '%' is not reflected."_sl, field.name, name);
			}
		}
	}
	
	
	// Build root signature stream (used by GraphicsApi):
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
			ArrayAppend(root_signature_stream, alloc, (u32)DivideAndRoundUp(ComputeTypeSize(template_type), sizeof(u32)));
			root_parameter_count += 1;
		}
	}
	
	// Write root signature metadata (used by runtime code):
	{
		auto& builder = root_signature_file.builder;
		
		builder.Append("%0::RootSignature %0::root_signature = {\n"_sl, name);
		builder.Indent();
		
		auto pass_type = PrintTypeValue(alloc, TypeInfoOf<CommandQueueType>(), &render_pass_note->pass_type);
		builder.Append("RootSignatureID{ % }, %, %,\n"_sl, (u32)root_signature_file.root_signatures.count, root_parameter_count, pass_type);
		
		u32 root_parameter_index = 0;
		for (auto& field : root_signature_type->fields) {
			if (field.type == &type_info_type) continue;
			
			auto root_argument_type = *FindNote<RootArgumentType>(field.type);
			
			auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
			if (root_argument_type == RootArgumentType::DescriptorTable) {
				builder.Append("{ %, % },\n"_sl, root_parameter_index, (u32)template_type->fields.count);
			} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
				builder.Append("{ % },\n"_sl, root_parameter_index);
			} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
				builder.Append("{ % },\n"_sl, root_parameter_index);
			}
			root_parameter_index += 1;
		}
		
		builder.Unindent();
		builder.Append("};\n"_sl);
		
		builder.Append("static u32 root_signature_stream_%[] = { "_sl, name);
		for (u32 dword : root_signature_stream) {
			builder.Append("%, "_sl, dword);
		}
		builder.Append("};\n\n"_sl);
	}
	
	GatherIncludesForDependentTypes(alloc, hlsl_bindings_file.includes, root_signature_type);
	
	
	// Write HLSL resource bindings (used by shader code):
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
					auto template_type_name = PrintTypeName(alloc, template_type);
					builder.Append("%<%> % : register(%.%);\n"_sl, descriptor_name, template_type_name, field.name, register_type, register_index);
				} else {
					builder.Append("% % : register(%.%);\n"_sl, descriptor_name, field.name, register_type, register_index);
				}
			}
		} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
			auto template_type_name = PrintTypeName(alloc, template_type);
			builder.Append("ConstantBuffer<%> % : register(b%);\n"_sl, template_type_name, field.name, cbv_index);
			cbv_index += 1;
		} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
			auto template_type_name = PrintTypeName(alloc, template_type);
			builder.Append("ConstantBuffer<%> % : register(b%);\n"_sl, template_type_name, field.name, cbv_index);
			cbv_index += 1;
		}
	}
	builder.Append("\n"_sl);
	
	
	RootSignaturePassData root_signature;
	root_signature.root_signature_stream = root_signature_stream;
	root_signature.include_file_name = filename;
	root_signature.render_pass_name  = name;
	ArrayAppend(root_signature_file.root_signatures, alloc, root_signature);
}


void WriteCodeForRenderPasses(StackAllocator* alloc, ArrayView<TypeInfo*> hlsl_file_type_infos, ArrayView<TypeInfoStruct*> render_pass_type_infos) {
	HashTable<String, HlslFileData> hlsl_files;
	HashTable<String, HlslFileData> hlsl_bindings_files;
	
	RootSignatureFileData root_signature_file;
	root_signature_file.builder.alloc = alloc;
	root_signature_file.builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	root_signature_file.builder.Append("#include \"Renderer/RenderPasses.h\"\n"_sl);
	root_signature_file.builder.Append("#include \"GraphicsApi/GraphicsApi.h\"\n\n"_sl);
	
	for (auto* type_info : hlsl_file_type_infos) {
		auto* hlsl_file_note = FindNote<Meta::HlslFile>(type_info);
		
		auto& hlsl_file = AddOrFindHlslFile(hlsl_files, alloc, hlsl_file_note->filename);
		GenerateCodeForHlslFile(alloc, hlsl_file, type_info);
	}
	
	for (auto* type_info : render_pass_type_infos) {
		auto* render_pass_note = FindNote<Meta::RenderPass>(type_info);
		
		auto render_pass_name = ExtractNameWithoutNamespace(type_info->name);
		auto filename = StringFormat(alloc, "%..hlsl"_sl, render_pass_name);
		
		auto& hlsl_file = AddOrFindHlslFile(hlsl_bindings_files, alloc, filename);
		GenerateCodeForRenderPass(alloc, filename, render_pass_name, hlsl_file, root_signature_file, type_info, render_pass_note);
	}
	
	WriteHlslFilesToDisk(alloc, hlsl_files);
	WriteHlslFilesToDisk(alloc, hlsl_bindings_files);
	
	auto& builder = root_signature_file.builder;
	{
		builder.Append("static String root_signature_filenames_internal[] = {\n"_sl);
		builder.Indent();
		
		for (auto& root_signature : root_signature_file.root_signatures) {
			builder.Append("\"%\"_sl,\n"_sl, root_signature.include_file_name);
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("ArrayView<String> root_signature_filenames = { root_signature_filenames_internal, % };\n\n"_sl, root_signature_file.root_signatures.count);
	}
	
	{
		builder.Append("static ArrayView<u32> root_signature_streams_internal[] = {\n"_sl);
		builder.Indent();
		
		for (auto& root_signature : root_signature_file.root_signatures) {
			builder.Append("{ root_signature_stream_%, % },\n"_sl, root_signature.render_pass_name, (u32)root_signature.root_signature_stream.count);
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("ArrayView<ArrayView<u32>> root_signature_streams = { root_signature_streams_internal, % };\n\n"_sl, root_signature_file.root_signatures.count);
	}
	
	{
		builder.Append("static CreatePipelinesCallback create_pipeline_callbacks_internal[] = {\n"_sl);
		builder.Indent();
		
		for (u32 i = 0; i < root_signature_file.root_signatures.count; i += 1) {
			auto name = root_signature_file.root_signatures[i].render_pass_name;
			builder.Append("&%::CreatePipelines,\n"_sl, name);
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("ArrayView<CreatePipelinesCallback> create_pipeline_callbacks = { create_pipeline_callbacks_internal, % };\n\n"_sl, root_signature_file.root_signatures.count);
	}
	
	WriteGeneratedFile(alloc, "Renderer/Generated/RootSignature.cpp"_sl, builder.ToString());
}
