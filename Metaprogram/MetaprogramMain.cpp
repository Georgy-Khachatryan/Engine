#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicSaveLoad.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "Engine/EntitySystem.h"
#include "TypeInfo.h"
#include "MetaprogramCommon.h"
#include "Tokens.h"

struct HlslFileData {
	HashTable<String, void> includes;
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

struct ShaderDefinitionData {
	String filename;
	String shader_name;
	u32 define_count = 0;
};

struct ShaderDefinitionFileData {
	StringBuilder builder;
	Array<ShaderDefinitionData> shader_definitions;
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

static void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfo* type_info) {
	auto& builder = hlsl_file.builder;
	
	auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
	DebugAssert(type_info_struct, "Meta::HlslFile must be applied to an struct.");
	
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	GatherIncludesForDependentTypes(alloc, hlsl_file.includes, type_info_struct);
	
	builder.Append("struct % {\n"_sl, name);
	builder.Indent();
	
	u32 constant_count = 0;
	for (auto& field : type_info_struct->fields) {
		DebugAssert(field.type != nullptr, "Type of field '%' in struct '%' is not reflected.", field.name, name);
		
		if (field.type == &type_info_type) {
			builder.Append("\n"_sl);
			GenerateCodeForHlslFile(alloc, hlsl_file, (TypeInfo*)field.constant_value);
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
			if (field.constant_value == nullptr) continue;
			
			auto type_name  = PrintTypeName(alloc, field.type);
			auto type_value = PrintTypeValue(alloc, field.type, field.constant_value);
			builder.Append("compile_const % %::% = %;\n"_sl, type_name, name, field.name, type_value);
		}
		builder.Append("\n"_sl);
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

static void GenerateCodeForRenderPass(StackAllocator* alloc, String filename, HlslFileData& hlsl_bindings_file, RootSignatureFileData& root_signature_file, TypeInfoStruct* type_info_struct, Meta::RenderPass* render_pass_note) {
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	TypeInfoStruct* root_signature_type = nullptr;
	for (auto& field : type_info_struct->fields) {
		if (field.name == "RootSignature"_sl) {
			root_signature_type = (TypeInfoStruct*)field.constant_value;
			break;
		}
	}
	DebugAssert(root_signature_type != nullptr, "RenderPass '%' is missing root signature.", name);
	
	// Validate root signature:
	for (auto& field : root_signature_type->fields) {
		if (field.type == &type_info_type) continue;
		
		auto* root_argument_type_note = FindNote<RootArgumentType>(field.type);
		if (root_argument_type_note == nullptr) {
			auto type_name = PrintTypeName(alloc, field.type);
			DebugAssertAlways("Unexpected field '%' of type '%' used in a root signature of pass '%'. Only root arguments are allowed.", field.name, type_name, name);
			continue;
		}
		auto root_argument_type = *root_argument_type_note;
		
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		if (root_argument_type == RootArgumentType::DescriptorTable) {
			DebugAssert(template_type, "Template type of DescriptorTable '%' in render pass '%' is not reflected.", field.name, name);
			
			for (auto& field : template_type->fields) {
				auto* descriptor_type_note = FindNote<ResourceDescriptorType>(field.type);
				if (descriptor_type_note == nullptr) {
					auto type_name = PrintTypeName(alloc, field.type);
					DebugAssertAlways("Unexpected field '%' of type '%' used in a descriptor table of pass '%'. Only descriptors are allowed.", field.name, type_name, name);
				}
			}
		} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
			DebugAssert(template_type, "Template type of ConstantBuffer '%' in render pass '%' is not reflected.", field.name, name);
		} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
			DebugAssert(template_type, "Template type of PushConstantBuffer '%' in render pass '%' is not reflected.", field.name, name);
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
			ArrayAppend(root_signature_stream, alloc, (u32)DivideAndRoundUp(ComputeTypeSize(template_type), sizeof(u32)));
			root_parameter_count += 1;
		}
	}
	
	{
		auto& cpp_builder = root_signature_file.builder;
		
		cpp_builder.Append("%0::RootSignature %0::root_signature = {\n"_sl, name);
		cpp_builder.Indent();
		
		auto pass_type = PrintTypeValue(alloc, TypeInfoOf<CommandQueueType>(), &render_pass_note->pass_type);
		cpp_builder.Append("RootSignatureID{ % }, %, %,\n"_sl, (u32)root_signature_file.root_signatures.count, root_parameter_count, pass_type);
		
		u32 root_parameter_index = 0;
		for (auto& field : root_signature_type->fields) {
			if (field.type == &type_info_type) continue;
			
			auto root_argument_type = *FindNote<RootArgumentType>(field.type);
			
			auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
			if (root_argument_type == RootArgumentType::DescriptorTable) {
				cpp_builder.Append("{ %, % },\n"_sl, root_parameter_index, (u32)template_type->fields.count);
			} else if (root_argument_type == RootArgumentType::ConstantBuffer) {
				cpp_builder.Append("{ % },\n"_sl, root_parameter_index);
			} else if (root_argument_type == RootArgumentType::PushConstantBuffer) {
				cpp_builder.Append("{ % },\n"_sl, root_parameter_index);
			}
			root_parameter_index += 1;
		}
		
		cpp_builder.Unindent();
		cpp_builder.Append("};\n"_sl);
		
		cpp_builder.Append("static u32 root_signature_stream_%[] = { "_sl, name);
		for (u32 dword : root_signature_stream) {
			cpp_builder.Append("%, "_sl, dword);
		}
		cpp_builder.Append("};\n\n"_sl);
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

static void GenerateCodeForRenderPass(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_bindings_files, RootSignatureFileData& root_signature_file, TypeInfo* type_info, Meta::RenderPass* render_pass_note) {
	auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
	DebugAssert(type_info_struct, "Meta::RenderPass must be applied to an struct.");
	
	auto render_pass_name = ExtractNameWithoutNamespace(type_info_struct->name);
	auto filename = StringFormat(alloc, "%..hlsl"_sl, render_pass_name);
	
	auto& hlsl_file = AddOrFindHlslFile(alloc, hlsl_bindings_files, filename);
	
	GenerateCodeForRenderPass(alloc, filename, hlsl_file, root_signature_file, type_info_struct, render_pass_note);
}

static void GenerateCodeForShaderDefinition(StackAllocator* alloc, ShaderDefinitionFileData& shader_definition_file, TypeInfo* type_info, Meta::ShaderName* note) {
	auto* type_info_enum = TypeInfoCast<TypeInfoEnum>(type_info);
	DebugAssert(type_info_enum, "Meta::ShaderName must be applied to an enum.");
	
	auto name = type_info_enum->name;
	
	auto& builder = shader_definition_file.builder;
	u32 define_count = 0;
	if (type_info_enum->fields.count != 0) {
		builder.Append("static String shader_defines_%[] = {\n"_sl, name);
		builder.Indent();
		
		for (auto& field : type_info_enum->fields) {
			if (CountSetBits(field.value) != 1) continue;
			
			DebugAssert(FirstBitLow(field.value) == define_count, "Out of order shader definition '%' in shader '%'. Position in enum: '%', Value: '1u << %'.", field.name, name, define_count, FirstBitLow(field.value));
			
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
			
			builder.Append("\"%\"_sl,\n"_sl, string);
			define_count += 1;
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	ShaderDefinitionData shader_definition;
	shader_definition.filename     = note->filename;
	shader_definition.shader_name  = name;
	shader_definition.define_count = define_count;
	ArrayAppend(shader_definition_file.shader_definitions, alloc, shader_definition);
}

static void RadixSort(StackAllocator* alloc, ArrayView<ComponentTypeID> keys, ArrayView<u32> values) {
	TempAllocationScope(alloc);
	
	DebugAssert(keys.count == values.count, "Mismatch between sort key and value count.");
	
	auto keys_swap   = ArrayViewAllocate<ComponentTypeID>(alloc, keys.count);
	auto values_swap = ArrayViewAllocate<u32>(alloc, keys.count);
	
	for (u32 offset = 0; offset < 32; offset += 8) {
		u32 prefix_sum[256] = {};
		
		for (u32 i = 0; i < keys.count; i += 1) {
			auto key = keys[i];
			u32 radix = (key.index >> offset) & 0xFF;
			prefix_sum[radix] += 1;
		}
		
		u32 running_prefix_sum = 0;
		for (u32 radix = 0; radix < 256; radix += 1) {
			u32 radix_count = prefix_sum[radix];
			prefix_sum[radix] = running_prefix_sum;
			running_prefix_sum += radix_count;
		}
		
		for (u32 i = 0; i < keys.count; i += 1) {
			auto key = keys[i];
			u32 radix = (key.index >> offset) & 0xFF;
			u32 output_index = prefix_sum[radix]++;
			
			keys_swap[output_index] = key;
			values_swap[output_index] = values[i];
		}
		
		Swap(keys_swap.data, keys.data);
		Swap(values_swap.data, values.data);
	}
}


struct VersionedField {
	String name;
	
	String type_name;
	
	u64 type_version   = 0;
	u64 constant_value = 0;
	
	u64 hash = 0;
};

struct VersionedTypeInfo {
	TypeInfoType info_type = TypeInfoType::None;
	bool generate_save_load_callback = false;
	
	struct VersionInfo {
		u64 version = 0;
		u64 hash    = 0;
		String underlying_type;
		ArrayView<VersionedField> fields;
	};
	
	Array<VersionInfo> versions;
};

compile_const auto save_load_versions_filepath = "Engine/SaveLoadVersions.txt"_sl;

HashTable<String, VersionedTypeInfo> ParseSaveLoadVersionHistory(StackAllocator* alloc) {
	auto file = SystemReadFileToString(alloc, save_load_versions_filepath);
	
	Tokenizer tokenizer;
	tokenizer.file     = file;
	tokenizer.filepath = save_load_versions_filepath;
	tokenizer.string   = file.data;
	tokenizer.alloc    = alloc;
	
	HashTable<String, VersionedTypeInfo> version_history;
	HashTableReserve(version_history, alloc, 128);
	
	auto token = tokenizer.PeekNextToken();
	while (token.type != TokenType::None) {
		auto info_type = tokenizer.ExpectToken(TokenType::Keyword);
		if (info_type.keyword != KeywordType::Enum && info_type.keyword != KeywordType::Struct) {
			tokenizer.ReportError(info_type, "Unexpected keyword, expected 'struct' or 'enum'."_sl);
		}
		auto is_enum = info_type.keyword == KeywordType::Enum;
		
		auto identifier = tokenizer.ExpectToken(TokenType::Identifier);
		tokenizer.ExpectToken(TokenType::OpeningBrace);
		
		auto [element, is_added] = HashTableAddOrFind(version_history, alloc, identifier.string, { is_enum ? TypeInfoType::Enum : TypeInfoType::Struct });
		if (is_added == false) {
			tokenizer.ReportError(identifier, "Type already exists."_sl);
		}
		auto& versions = element->value.versions;
		
		u64 latest_version = 0;
		u64 latest_version_index = 0;
		
		u64 identifier_hash = ComputeHash(identifier.string);
		
		token = tokenizer.PeekNextToken();
		while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
			auto version         = tokenizer.ExpectToken(TokenType::Number);
			auto underlying_type = is_enum ? tokenizer.ExpectToken(TokenType::Identifier) : Token{};
			
			tokenizer.ExpectToken(TokenType::OpeningBrace);
			
			u64 hash = identifier_hash;
			Array<VersionedField> fields;
			
			token = tokenizer.PeekNextToken();
			while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
				auto identifier = tokenizer.ExpectToken(TokenType::Identifier);
				auto type_name  = is_enum ? underlying_type : tokenizer.ExpectToken(TokenType::Identifier);
				auto type_version_or_constant_value = tokenizer.ExpectToken(TokenType::Number);
				
				tokenizer.ExpectToken(TokenType::Semicolon);
				
				VersionedField field;
				field.name           = identifier.string;
				field.type_name      = type_name.string;
				field.type_version   = is_enum ? 0 : StringToU64(type_version_or_constant_value.string);
				field.constant_value = is_enum ? StringToU64(type_version_or_constant_value.string) : 0;
				field.hash           = ComputeHash64(ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)), is_enum ? field.constant_value : field.type_version);
				ArrayAppend(fields, alloc, field);
				
				hash = ComputeHash64(field.hash, hash);
				
				token = tokenizer.PeekNextToken();
			}
			tokenizer.ExpectToken(TokenType::ClosingBrace);
			
			VersionedTypeInfo::VersionInfo version_info;
			version_info.version = StringToU64(version.string);
			version_info.hash    = hash;
			version_info.underlying_type = underlying_type.string;
			version_info.fields  = fields;
			ArrayAppend(versions, alloc, version_info);
			
			if (version_info.version > latest_version) {
				latest_version       = version_info.version;
				latest_version_index = versions.count - 1;
			}
			
			token = tokenizer.PeekNextToken();
		}
		tokenizer.ExpectToken(TokenType::ClosingBrace);
		
		if (element->value.versions.count == 0) {
			tokenizer.ReportError(identifier, "Empty versioned type has no versions defined."_sl);
		}
		
		// Put the latest version at the end of the array.
		if (latest_version_index != versions.count - 1) {
			Swap(versions[versions.count - 1], versions[latest_version_index]);
		}
		
		token = tokenizer.PeekNextToken();
	}
	
	return version_history;
}

u64 AddVersionedTypeToSaveLoadHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo>& version_history, String name, TypeInfoType info_type, VersionedTypeInfo::VersionInfo new_version) {
	auto [element, is_added] = HashTableAddOrFind(version_history, alloc, name, { info_type });
	
	auto& type = element->value;
	type.generate_save_load_callback = true;
	
	if (is_added || ArrayLastElement(type.versions).hash != new_version.hash) {
		new_version.version = is_added ? 0 : ArrayLastElement(type.versions).version + 1;
		ArrayAppend(type.versions, alloc, new_version);
	}
	
	return ArrayLastElement(type.versions).version;
}

u64 AddTypeInfoToSaveLoadHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo>& version_history, TypeInfo* type_info) {
	u64 result_version = 0;
	
	if (type_info->info_type == TypeInfoType::Struct) {
		auto* type_info_struct = (TypeInfoStruct*)type_info;
		
		VersionedTypeInfo::VersionInfo info;
		info.hash = ComputeHash(type_info_struct->name);
		
		Array<VersionedField> fields;
		ArrayReserve(fields, alloc, type_info_struct->fields.count);
		for (auto& field : type_info_struct->fields) {
			if (field.type == &type_info_type) continue;
			
			DebugAssert(field.type != nullptr, "Type of field '%' in struct '%' is not reflected.", field.name, type_info_struct->name);
			DebugAssert(field.type->info_type != TypeInfoType::Pointer, "Pointer SaveLoad is not supported. Field '%' in struct '%'.", field.name, type_info_struct->name);
			
			u64 type_version = AddTypeInfoToSaveLoadHistory(alloc, version_history, field.type);
			
			VersionedField version_field;
			version_field.name           = field.name;
			version_field.type_name      = PrintTypeName(alloc, field.type);
			version_field.type_version   = type_version;
			version_field.constant_value = 0;
			version_field.hash           = ComputeHash64(ComputeHash64(ComputeHash(version_field.name), ComputeHash(version_field.type_name)), version_field.type_version);
			ArrayAppend(fields, alloc, version_field);
			
			info.hash = ComputeHash64(version_field.hash, info.hash);
		}
		
		info.fields = fields;
		
		result_version = AddVersionedTypeToSaveLoadHistory(alloc, version_history, type_info_struct->name, TypeInfoType::Struct, info);
	} else if (type_info->info_type == TypeInfoType::Enum) {
		auto* type_info_enum = (TypeInfoEnum*)type_info;
		
		auto type_name = PrintTypeName(alloc, type_info_enum->underlying_type);
		
		VersionedTypeInfo::VersionInfo info;
		info.hash = ComputeHash(type_info_enum->name);
		info.underlying_type = type_name;
		
		Array<VersionedField> fields;
		ArrayReserve(fields, alloc, type_info_enum->fields.count);
		
		for (auto& field : type_info_enum->fields) {
			VersionedField version_field;
			version_field.name           = field.name;
			version_field.type_name      = type_name;
			version_field.type_version   = 0;
			version_field.constant_value = field.value;
			version_field.hash           = ComputeHash64(ComputeHash64(ComputeHash(version_field.name), ComputeHash(version_field.type_name)), version_field.constant_value);
			ArrayAppend(fields, alloc, version_field);
			
			info.hash = ComputeHash64(version_field.hash, info.hash);
		}
		
		info.fields = fields;
		
		result_version = AddVersionedTypeToSaveLoadHistory(alloc, version_history, type_info_enum->name, TypeInfoType::Enum, info);
	}
	
	return result_version;
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
	root_signature_file.builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	root_signature_file.builder.Append("#include \"Engine/RenderPasses.h\"\n"_sl);
	root_signature_file.builder.Append("#include \"GraphicsApi/GraphicsApi.h\"\n\n"_sl);
	
	ShaderDefinitionFileData shader_definition_file;
	shader_definition_file.builder.alloc = &alloc;
	shader_definition_file.builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	shader_definition_file.builder.Append("#include \"Basic/BasicString.h\"\n"_sl);
	shader_definition_file.builder.Append("#include \"Engine/RenderPasses.h\"\n\n"_sl);
	
	Array<TypeInfoStruct*> entity_type_infos;
	Array<TypeInfoStruct*> entity_query_type_infos;
	
	StringBuilder entity_system_builder;
	entity_system_builder.alloc = &alloc;
	entity_system_builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	entity_system_builder.Append("#include \"Engine/Entities.h\"\n\n"_sl);
	
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
		
		if (auto* note = FindNote<Meta::EntityType>(type_info)) {
			auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
			DebugAssert(type_info_struct, "Meta::EntityType must be applied to a struct.");
			ArrayAppend(entity_type_infos, &alloc, type_info_struct);
		}
		
		if (auto* note = FindNote<Meta::ComponentQuery>(type_info)) {
			auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
			DebugAssert(type_info_struct, "Meta::ComponentQuery must be applied to a struct.");
			ArrayAppend(entity_query_type_infos, &alloc, type_info_struct);
		}
	}
	
	auto version_history = ParseSaveLoadVersionHistory(&alloc);
	
	{
		HashTable<TypeInfoStruct*, u32> component_types;
		Array<TypeInfoStruct*>     component_type_infos;
		
		Array<EntityTypeInfo> runtime_entity_type_infos;
		ArrayReserve(runtime_entity_type_infos, &alloc, entity_type_infos.count);
		
		for (auto* type_info : entity_type_infos) {
			Array<ComponentTypeID> component_type_ids;
			ArrayReserve(component_type_ids, &alloc, type_info->fields.count);
			
			for (auto& field : type_info->fields) {
				auto* component_type_info = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
				DebugAssert(component_type_info, "Unknown component type.");
				
				auto [element, is_added] = HashTableAddOrFind(component_types, &alloc, component_type_info, (u32)component_types.count);
				if (is_added) ArrayAppend(component_type_infos, &alloc, component_type_info);
				
				ArrayAppend(component_type_ids, { element->value });
			}
			
			Array<u32> component_stream_indices;
			ArrayResize(component_stream_indices, &alloc, component_type_ids.count);
			for (u32 i = 0; i < component_stream_indices.count; i += 1) {
				component_stream_indices[i] = i;
			}
			
			RadixSort(&alloc, component_type_ids, component_stream_indices);
			
			EntityTypeInfo entity_type_info;
			entity_type_info.component_type_ids = component_type_ids;
			ArrayAppend(runtime_entity_type_infos, entity_type_info);
		}
		
		Array<EntityQueryTypeInfo> runtime_entity_query_type_infos;
		ArrayReserve(runtime_entity_query_type_infos, &alloc, entity_query_type_infos.count);
		
		for (auto* type_info : entity_query_type_infos) {
			Array<ComponentTypeID> component_type_ids;
			ArrayReserve(component_type_ids, &alloc, type_info->fields.count);
			
			for (auto& field : type_info->fields) {
				auto* component_stream_type_info = TypeInfoCast<TypeInfoPointer>(field.type);
				DebugAssert(component_stream_type_info, "Unknown component type");
				
				auto* component_type_info = TypeInfoCast<TypeInfoStruct>(component_stream_type_info->pointer_to);
				DebugAssert(component_type_info, "Unknown component type");
				
				auto [element, is_added] = HashTableAddOrFind(component_types, &alloc, component_type_info, (u32)component_types.count);
				if (is_added) ArrayAppend(component_type_infos, &alloc, component_type_info);
				
				ArrayAppend(component_type_ids, { element->value });
			}
			
			Array<u32> component_stream_indices;
			ArrayResize(component_stream_indices, &alloc, component_type_ids.count);
			for (u32 i = 0; i < component_stream_indices.count; i += 1) {
				component_stream_indices[i] = i;
			}
			
			RadixSort(&alloc, component_type_ids, component_stream_indices);
			
			EntityQueryTypeInfo entity_query_type_info;
			entity_query_type_info.component_type_ids       = component_type_ids;
			entity_query_type_info.component_stream_indices = component_stream_indices;
			ArrayAppend(runtime_entity_query_type_infos, entity_query_type_info);
		}
		
		auto& builder = entity_system_builder;
		for (u32 entity_type_index = 0; entity_type_index < entity_type_infos.count; entity_type_index += 1) {
			auto* type_info = entity_type_infos[entity_type_index];
			builder.Append("EntityTypeID ESC::GetEntityTypeID<%>::id = { % };\n"_sl, type_info->name, entity_type_index);
		}
		builder.Append("\n"_sl);
		
		for (u32 entity_query_type_index = 0; entity_query_type_index < entity_query_type_infos.count; entity_query_type_index += 1) {
			auto* type_info = entity_query_type_infos[entity_query_type_index];
			builder.Append("EntityQueryTypeID ESC::GetEntityQueryTypeID<%>::id = { % };\n"_sl, type_info->name, entity_query_type_index);
		}
		builder.Append("\n"_sl);
		
		for (u32 component_type_index = 0; component_type_index < component_type_infos.count; component_type_index += 1) {
			auto* type_info = component_type_infos[component_type_index];
			builder.Append("ComponentTypeID ESC::GetComponentTypeID<%>::id = { % };\n"_sl, type_info->name, component_type_index);
		}
		builder.Append("\n"_sl);
		
		
		for (u32 entity_type_index = 0; entity_type_index < entity_type_infos.count; entity_type_index += 1) {
			auto* type_info = entity_type_infos[entity_type_index];
			auto& runtime_type_info = runtime_entity_type_infos[entity_type_index];
			
			builder.Append("static ComponentTypeID %_component_type_ids[] = { "_sl, type_info->name);
			for (auto component_type_id : runtime_type_info.component_type_ids) {
				builder.Append("%, "_sl, component_type_id.index);
			}
			builder.Append("};\n"_sl);
		}
		builder.Append("\n"_sl);
		
		for (u32 entity_query_type_index = 0; entity_query_type_index < entity_query_type_infos.count; entity_query_type_index += 1) {
			auto* type_info = entity_query_type_infos[entity_query_type_index];
			auto& runtime_type_info = runtime_entity_query_type_infos[entity_query_type_index];
			
			builder.Append("static ComponentTypeID %_component_type_ids[] = { "_sl, type_info->name);
			for (auto component_type_id : runtime_type_info.component_type_ids) {
				builder.Append("%, "_sl, component_type_id.index);
			}
			builder.Append("};\n"_sl);
			
			builder.Append("static u32 %_component_stream_indices[] = { "_sl, type_info->name);
			for (u32 component_stream_index : runtime_type_info.component_stream_indices) {
				builder.Append("%, "_sl, component_stream_index);
			}
			builder.Append("};\n"_sl);
		}
		builder.Append("\n"_sl);
		
		
		builder.Append("static EntityTypeInfo entity_type_info_table_internal[] = {\n"_sl);
		builder.Indent();
		for (u32 entity_type_index = 0; entity_type_index < entity_type_infos.count; entity_type_index += 1) {
			auto* type_info = entity_type_infos[entity_type_index];
			auto& runtime_type_info = runtime_entity_type_infos[entity_type_index];
			
			builder.Append("{ { %_component_type_ids, % }, 0x%x },\n"_sl, type_info->name, (u32)runtime_type_info.component_type_ids.count, ComputeHash(type_info->name));
		}
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		
		builder.Append("static EntityQueryTypeInfo entity_query_type_info_table_internal[] = {\n"_sl);
		builder.Indent();
		for (u32 entity_query_type_index = 0; entity_query_type_index < entity_query_type_infos.count; entity_query_type_index += 1) {
			auto* type_info = entity_query_type_infos[entity_query_type_index];
			auto& runtime_type_info = runtime_entity_query_type_infos[entity_query_type_index];
			
			u32 component_count = (u32)runtime_type_info.component_type_ids.count;
			builder.Append("{ { %0_component_type_ids, %1 }, { %0_component_stream_indices, %1 } },\n"_sl, type_info->name, component_count);
		}
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("ComponentTypeInfo component_type_info_table_internal[] = {\n"_sl);
		builder.Indent();
		for (auto* type_info : component_type_infos) {
			u64 version = AddTypeInfoToSaveLoadHistory(&alloc, version_history, type_info);
			builder.Append("{ %, %, 0x%x },\n"_sl, type_info->size, version, ComputeHash(type_info->name));
		}
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		for (auto* type_info : component_type_infos) {
			builder.Append("extern void SaveLoad(SaveLoadBuffer& buffer, %& component, u64 version);\n"_sl, type_info->name);
		}
		builder.Append("\n"_sl);
		
		builder.Append("SaveLoadCallback component_save_load_callbacks_internal[] = {\n"_sl);
		builder.Indent();
		for (auto* type_info : component_type_infos) {
			builder.Append("[](SaveLoadBuffer& buffer, void* data, u64 version) { SaveLoad(buffer, *(%*)data, version); },\n"_sl, type_info->name);
		}
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("DefaultInitializeCallback component_default_initialize_callbacks_internal[] = {\n"_sl);
		builder.Indent();
		for (auto* type_info : component_type_infos) {
			builder.Append("[](void* data, u64 begin, u64 end) { for (u64 i = begin; i < end; i += 1) ((%*)data)[i] = {}; },\n"_sl, type_info->name);
		}
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		
		builder.Append("ArrayView<EntityTypeInfo> entity_type_info_table = { entity_type_info_table_internal, % };\n"_sl, entity_type_infos.count);
		builder.Append("ArrayView<EntityQueryTypeInfo> entity_query_type_info_table = { entity_query_type_info_table_internal, % };\n"_sl, entity_query_type_infos.count);
		builder.Append("ArrayView<ComponentTypeInfo> component_type_info_table = { component_type_info_table_internal, % };\n"_sl, component_type_infos.count);
		builder.Append("ArrayView<SaveLoadCallback> component_save_load_callbacks = { component_save_load_callbacks_internal, % };\n"_sl, component_type_infos.count);
		builder.Append("ArrayView<DefaultInitializeCallback> component_default_initialize_callbacks = { component_default_initialize_callbacks_internal, % };\n"_sl, component_type_infos.count);
	}
	
	{
		StringBuilder builder;
		builder.alloc = &alloc;
		builder.Append("#include \"Basic/Basic.h\"\n"_sl);
		builder.Append("#include \"Basic/BasicSaveLoad.h\"\n"_sl);
		builder.Append("#include \"Engine/Entities.h\"\n\n"_sl);
		
		compile_const auto save_load_dummy_suffix = "_Dummy"_sl;
		
		// Create dummy enum struct types for any removed types. They are only used for function overload resolution.
		for (auto& [name, type] : version_history) {
			if (type.generate_save_load_callback == false) {
				builder.Append("enum struct %.% : u32;\n"_sl, name, save_load_dummy_suffix);
			}
		}
		
		for (auto& [name, type] : version_history) {
			auto suffix = type.generate_save_load_callback ? ""_sl : "_Dummy"_sl;
			builder.Append("void SaveLoad(SaveLoadBuffer& buffer, %.%& data, u64 version);\n"_sl, name, suffix);
		}
		builder.Append("\n"_sl);
		
		
		for (auto& [name, type] : version_history) {
			bool is_enum = type.info_type == TypeInfoType::Enum;
			
			auto suffix = type.generate_save_load_callback ? ""_sl : save_load_dummy_suffix;
			builder.Append("void SaveLoad(SaveLoadBuffer& buffer, %.%& data, u64 version) {\n"_sl, name, suffix);
			
 			HashTable<u64, u64> new_field_table;
			HashTableReserve(new_field_table, &alloc, ArrayLastElement(type.versions).fields.count);
			
			for (auto& field : ArrayLastElement(type.versions).fields) {
				HashTableAddOrFind(new_field_table, ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)), field.constant_value);
			}
			
			builder.Indent();
			
			for (s64 i = type.versions.count - 1; i >= 0; i -= 1) {
				auto& version = type.versions[i];
				
				bool is_latest_version = i == (type.versions.count - 1);
				
				if (type.versions.count != 1) {
					if (is_latest_version) {
						if (type.generate_save_load_callback) {
							builder.Append("DebugAssert(version == % || buffer.is_loading, \"Old versions can only be loaded.\");\n"_sl, version.version);
						} else {
							builder.Append("DebugAssert(buffer.is_loading, \"Old versions can only be loaded.\");\n"_sl);
						}
						builder.Append("if (version == %) {\n"_sl, version.version);
					} else {
						builder.Append("} else if (version == %) {\n"_sl, version.version);
					}
					
					builder.Indent();
				}
				
				if (is_enum) {
					if (is_latest_version && type.generate_save_load_callback) {
						builder.Append("buffer.SaveLoadBytes(&data, sizeof(data));\n"_sl);
					} else {
						builder.Append("% value = 0;\n"_sl, version.underlying_type);
						builder.Append("SaveLoad(buffer, value);\n"_sl);
						
						if (type.generate_save_load_callback) {
							builder.Append("switch (value) {\n"_sl);
							for (auto& field : version.fields) {
								auto* new_field = HashTableFind(new_field_table, ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)));
								
								if (new_field != nullptr) {
									builder.Append("case %: data = (%)%; break;\n"_sl, field.constant_value, name, new_field->value);
								}
							}
							builder.Append("default: data = {}; break;\n"_sl);
							builder.Append("}\n"_sl);
						}
					}
				} else {
					if (is_latest_version == false && type.generate_save_load_callback) {
						// Default initialize when loading old data (might have incomplete set of fields).
						builder.Append("data = {};\n"_sl);
					}
					
					for (auto& field : version.fields) {
						bool has_new_field = HashTableFind(new_field_table, ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name))) != nullptr;
						
						if (has_new_field && type.generate_save_load_callback) {
							builder.Append("SaveLoad(buffer, data.%, %);\n"_sl, field.name, field.type_version);
						} else {
							auto* element = HashTableFind(version_history, field.type_name);
							bool use_skip_callback = (element != nullptr) && (element->value.generate_save_load_callback == false);
							
							auto suffix = use_skip_callback ? save_load_dummy_suffix : ""_sl;
							builder.Append("SaveLoadDummy<%.%>(buffer, %);\n"_sl, field.type_name, suffix, field.type_version);
						}
					}
				}
				
				builder.Unindent();
			}
			
			if (type.versions.count != 1) {
				builder.Append("}\n"_sl);
				builder.Unindent();
			}
			
			builder.Append("}\n\n"_sl);
		}
		
		WriteGeneratedFile(&alloc, "Engine/Generated/SaveLoadCallbacks.cpp"_sl, builder.ToString());
	}
	
	{
		StringBuilder builder;
		builder.alloc = &alloc;
		
		for (auto& [name, type] : version_history) {
			bool is_enum = type.info_type == TypeInfoType::Enum;
			
			builder.Append("% % {\n"_sl, is_enum ? "enum"_sl : "struct"_sl, name);
			builder.Indent();
			
			for (auto& version : type.versions) {
				if (is_enum) {
					builder.Append("/*Version*/ % % {\n"_sl, version.version, version.underlying_type);
				} else {
					builder.Append("/*Version*/ % {\n"_sl, version.version);
				}
				builder.Indent();
				
				if (is_enum) {
					for (auto& field : version.fields) {
						builder.Append("% %;\n"_sl, field.name, field.constant_value);
					}
				} else {
					for (auto& field : version.fields) {
						builder.Append("% % %;\n"_sl, field.name, field.type_name, field.type_version);
					}
				}
				
				builder.Unindent();
				builder.Append("}\n"_sl);
			}
			
			builder.Unindent();
			builder.Append("}\n\n"_sl);
		}
		
		WriteGeneratedFile(&alloc, save_load_versions_filepath, builder.ToString());
	}
	
	EnsureDirectoryExists(&alloc, "Shaders/Generated/"_sl);
	EnsureDirectoryExists(&alloc, "Engine/Generated/"_sl);
	
	WriteHlslFilesToDisk(&alloc, hlsl_files);
	WriteHlslFilesToDisk(&alloc, hlsl_bindings_files);
	
	{
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
		
		WriteGeneratedFile(&alloc, "Engine/Generated/RootSignature.cpp"_sl, builder.ToString());
	}
	
	{
		auto& builder = shader_definition_file.builder;
		
		for (u64 i = 0; i < shader_definition_file.shader_definitions.count; i += 1) {
			builder.Append("ShaderID %ID = { % };\n"_sl, shader_definition_file.shader_definitions[i].shader_name, (u32)i);
		}
		builder.Append("\n"_sl);
		
		
		builder.Append("static ShaderDefinition shader_definition_table_internal[] = {\n"_sl);
		builder.Indent();
		
		for (auto& definition : shader_definition_file.shader_definitions) {
			if (definition.define_count != 0) {
				builder.Append("{ \"%\"_sl, ArrayView<String>{ shader_defines_%, % } },\n"_sl, definition.filename, definition.shader_name, definition.define_count);
			} else {
				builder.Append("{ \"%\"_sl, ArrayView<String>{} },\n"_sl, definition.filename);
			}
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("ArrayView<ShaderDefinition> shader_definition_table = { shader_definition_table_internal, % };\n\n"_sl, shader_definition_file.shader_definitions.count);
		
		WriteGeneratedFile(&alloc, "Engine/Generated/ShaderDefinitions.cpp"_sl, builder.ToString());
	}
	
	{
		WriteGeneratedFile(&alloc, "Engine/Generated/EntitySystemMetadata.cpp"_sl, entity_system_builder.ToString());
	}
	
	return 0;
}

