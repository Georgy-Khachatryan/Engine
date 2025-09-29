#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "Engine/RenderPasses.h"
#include "Tokens.h"
#include "AstNodes.h"
#include "TypeInfo.h"

FORWARD_DECLARE_NOTE(Meta::RenderGraphSystem);
FORWARD_DECLARE_NOTE(Meta::RenderPass);
FORWARD_DECLARE_NOTE(Meta::HlslFile);

template<typename T>
inline T* TypeInfoCast(TypeInfo* type_info) {
	return type_info && type_info->info_type == T::my_type ? (T*)type_info : nullptr;
}

template<typename T>
static T* FindNote(ArrayView<TypeInfoNote> notes) {
	auto* type_info = TypeInfoOf<T>();
	for (auto& note : notes) {
		if (note.type == type_info) return (T*)note.value;
	}
	return nullptr;
}

template<typename T>
static T* FindNote(TypeInfoStruct* type_info) { return FindNote<T>(type_info->notes); }

template<typename T>
static T* FindNote(TypeInfo* type_info) {
	if (type_info == nullptr) return nullptr;
	
	switch (type_info->info_type) {
	case TypeInfoType::Struct: return FindNote<T>(static_cast<TypeInfoStruct*>(type_info)->notes);
	default: return nullptr;
	}
}

static TypeInfo* ExtractTemplateParameterType(TypeInfo* type_info, u32 index) {
	if (type_info == nullptr) return nullptr;
	if (type_info->info_type != TypeInfoType::Struct) return nullptr;
	
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	if (index >= type_info_struct->fields.count) return nullptr;
	
	auto& field = type_info_struct->fields[index];
	if (field.type != &type_info_type) return nullptr;
	if (HasAnyFlags(field.flags, TypeInfoStructFieldFlags::TemplateParameter) == false) return nullptr;
	
	return (TypeInfo*)field.constant_value;
}

static String ExtractNameWithoutNamespace(String name) {
	u64 offset = name.count;
	while (offset != 0 && name[offset - 1] != ':') {
		offset -= 1;
	}
	
	name.data  += offset;
	name.count -= offset;
	
	return name;
}

struct HlslFileData {
	StringBuilder builder;
	String include_guard;
};

static String PrintTypeName(TypeInfo* type_info) {
	switch (type_info ? type_info->info_type : TypeInfoType::None) {
	case TypeInfoType::Integer: {
		auto* type_info_integer = (TypeInfoInteger*)type_info;
		compile_const u32 is_signed_flag = 128;
		switch (type_info_integer->bit_width | (type_info_integer->is_signed ? is_signed_flag : 0)) {
		case 1:  return "bool"_sl;
		case 8:  return "u8"_sl;
		case 16: return "u16"_sl;
		case 32: return "u32"_sl;
		case 64: return "u64"_sl;
		case 8  | is_signed_flag: return "s8"_sl;
		case 16 | is_signed_flag: return "s16"_sl;
		case 32 | is_signed_flag: return "s32"_sl;
		case 64 | is_signed_flag: return "s64"_sl;
		default: return "Unknown Integer"_sl;
		}
	} case TypeInfoType::Float: {
		auto* type_info_float = (TypeInfoFloat*)type_info;
		switch (type_info_float->bit_width) {
		case 32: return "float"_sl;
		case 64: return "double"_sl;
		default: return "Unknown Float"_sl;
		}
	} case TypeInfoType::Struct: {
		auto* type_info_struct = (TypeInfoStruct*)type_info;
		return type_info_struct->name;
	} case TypeInfoType::Type: {
		return "Type"_sl;
	} case TypeInfoType::Void: {
		return "void"_sl;
	} case TypeInfoType::String: {
		return "String"_sl;
	} case TypeInfoType::None: {
		return "None"_sl;
	} default: {
		DebugAssertAlways("Unhandled TypeInfoType.");
		return "Unknown Type"_sl;
	}
	}
}

void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfo* type_info) {
	auto& builder = hlsl_file.builder;
	
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	// TODO: Dependent type includes?
	
	builder.Append("struct %.*s {\n", (s32)name.count, name.data);
	builder.Indent();
	
	for (auto& field : type_info_struct->fields) {
		if (field.type == &type_info_type) {
			builder.Append("\n");
			GenerateCodeForHlslFile(alloc, hlsl_file, (TypeInfo*)field.constant_value);
		} else if (field.constant_value) {
			auto type_name = PrintTypeName(field.type);
			builder.Append("compile_const %.*s %.*s = %u;\n", (s32)type_name.count, type_name.data, (s32)field.name.count, field.name.data, *(u32*)field.constant_value);
		} else {
			auto type_name = PrintTypeName(field.type);
			builder.Append("%.*s %.*s;\n", (s32)type_name.count, type_name.data, (s32)field.name.count, field.name.data);
		}
	}
	
	builder.Unindent();
	builder.AppendUnformatted("};\n\n"_sl);
}

HlslFileData& AddOrFindHlslFile(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files, String filename) {
	auto [element, is_added] = HashTableAddOrFind(hlsl_files, alloc, filename, HlslFileData{});
	auto& hlsl_file = element->value;
	auto& builder = hlsl_file.builder;
	
	if (is_added) {
		auto include_guard = StringFormat(alloc, "GENERATED_%.*s", (s32)filename.count, filename.data);
		for (u64 i = 0; i < include_guard.count; i += 1) {
			auto c = include_guard[i];
			
			if (c == '.') {
				include_guard[i] = '_';
			} else if (c >= 'a' && c <= 'z') {
				include_guard[i] = c - 'a' + 'A';
			}
		}
		hlsl_file.include_guard = include_guard;
		
		builder.alloc = alloc;
		builder.Append("#ifndef %.*s\n", (s32)include_guard.count, include_guard.data);
		builder.Append("#define %.*s\n", (s32)include_guard.count, include_guard.data);
		builder.AppendUnformatted("#include \"Basic.hlsl\"\n\n"_sl);
	}
	
	return hlsl_file;
}

void GenerateCodeForHlslFile(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files, TypeInfo* type_info, Meta::HlslFile* hlsl_file_note) {
	auto& hlsl_file = AddOrFindHlslFile(alloc, hlsl_files, hlsl_file_note->filename);

	GenerateCodeForHlslFile(alloc, hlsl_file, type_info);
}

static void WriteHlslFilesToDisk(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_files) {
	SystemCreateDirectory(alloc, "./Shaders/Generated/"_sl);
	
	u8* metadata = hlsl_files.metadata;
	auto* data   = hlsl_files.data;
	
	for (u64 i = 0; i < hlsl_files.capacity; i += 1) {
		if (metadata[i] <= hash_table_hash_value_deleted) continue;
		
		auto& element = hlsl_files.data[i];
		auto& hlsl_file = element.value;
		auto& builder = hlsl_file.builder;
		
		builder.Append("#endif // %.*s\n", (s32)hlsl_file.include_guard.count, hlsl_file.include_guard.data);
		
		auto output_filepath = StringFormat(alloc, "./Shaders/Generated/%.*s", (s32)element.key.count, element.key.data);
		auto output_file = SystemOpenFile(alloc, output_filepath, OpenFileFlags::Write);
		if (output_file.handle == nullptr) {
			SystemWriteToConsole(alloc, "Failed to open output file '%s'.\n", output_filepath.data);
			SystemExitProcess(1);
		}
		
		auto file_string = builder.ToString();
		SystemWriteFile(output_file, file_string.data, file_string.count, 0);
		SystemCloseFile(output_file);
	}
}

static void GenerateCodeForRenderPass(StackAllocator* alloc, HlslFileData& hlsl_bindings_file, HlslFileData& root_signature_file, TypeInfo* type_info) {
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	auto& builder = hlsl_bindings_file.builder;
	
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	HashTable<String, u32> dependent_types;
	
	TypeInfoStruct* root_signature_type = nullptr;
	for (auto& field : type_info_struct->fields) {
		if (field.name == "RootSignature"_sl) {
			root_signature_type = (TypeInfoStruct*)field.constant_value;
			
			for (auto& field : root_signature_type->fields) {
				auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
				if (template_type == nullptr) continue;
				
				auto type_name = PrintTypeName(field.type);
				if (type_name == "HLSL::DescriptorTable<T>"_sl) {
					for (auto& field : template_type->fields) {
						auto* template_type = ExtractTemplateParameterType(field.type, 0);
						if (auto* note = FindNote<Meta::HlslFile>(template_type)) {
							HashTableAddOrFind(dependent_types, alloc, note->filename, 0u);
						}
					}
				} else if (type_name == "HLSL::ConstantBuffer<T>"_sl) {
					if (auto* note = FindNote<Meta::HlslFile>(template_type)) {
						HashTableAddOrFind(dependent_types, alloc, note->filename, 0u);
					}
				}
			}
		}
	}
	DebugAssert(root_signature_type != nullptr, "RenderPass '%.*s' is missing root signature.", (s32)name.count, name.data);
	
	if (dependent_types.count != 0) {
		u8* metadata = dependent_types.metadata;
		auto* data   = dependent_types.data;
		
		for (u64 i = 0; i < dependent_types.capacity; i += 1) {
			if (metadata[i] <= hash_table_hash_value_deleted) continue;
			
			auto& element = data[i];
			auto filename = element.key;
			
			builder.Append("#include \"%.*s\"\n", (s32)filename.count, filename.data);
		}
		builder.AppendUnformatted("\n"_sl);
	}
	
	u32 cbv_index = 0;
	u32 srv_index = 0;
	u32 uav_index = 0;
	
	auto& cpp_builder = root_signature_file.builder;
	
	cpp_builder.Append("%.*s::RootSignature %.*s::root_signature = {\n", (s32)name.count, name.data, (s32)name.count, name.data);
	cpp_builder.Indent();
	
	u32 root_parameter_index = 0;
	for (auto& field : root_signature_type->fields) {
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		
		auto type_name = PrintTypeName(field.type);
		if (type_name == "HLSL::DescriptorTable<T>"_sl) {
			for (auto& field : template_type->fields) {
				auto* template_type = ExtractTemplateParameterType(field.type, 0);
				auto template_type_name = PrintTypeName(template_type);
				
				auto type_name = PrintTypeName(field.type);
				if (type_name == "HLSL::Texture2D<T>"_sl) {
					builder.Append("Texture2D<%.*s> %.*s : register(t%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, srv_index);
					srv_index += 1;
				} else if (type_name == "HLSL::RWTexture2D<T>"_sl) {
					builder.Append("RWTexture2D<%.*s> %.*s : register(u%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, uav_index);
					uav_index += 1;
				} else if (type_name == "HLSL::RegularBuffer<T>"_sl) {
					builder.Append("StructuredBuffer<%.*s> %.*s : register(t%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, srv_index);
					srv_index += 1;
				} else if (type_name == "HLSL::RWRegularBuffer<T>"_sl) {
					builder.Append("RWStructuredBuffer<%.*s> %.*s : register(u%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, uav_index);
					uav_index += 1;
				} else {
					DebugAssertAlways("Unexpected field '%.*s' of type '%.*s' used in a descriptor table of pass '%.*s'. Only descriptors are allowed.", (s32)field.name.count, field.name.data, (s32)type_name.count, type_name.data, (s32)name.count, name.data);
				}
			}
			
			cpp_builder.Append("{ %u, %u },\n", root_parameter_index, (u32)template_type->fields.count);
			root_parameter_index += 1;
		} else if (type_name == "HLSL::ConstantBuffer<T>"_sl) {
			auto template_type_name = PrintTypeName(template_type);
			
			builder.Append("ConstantBuffer<%.*s> %.*s : register(b%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, cbv_index);
			cbv_index += 1;
			
			cpp_builder.Append("{ %u },\n", root_parameter_index);
			root_parameter_index += 1;
		} else if (field.type != &type_info_type) {
			DebugAssertAlways("Unexpected field '%.*s' of type '%.*s' used in a root signature of pass '%.*s'. Only root arguments are allowed.", (s32)field.name.count, field.name.data, (s32)type_name.count, type_name.data, (s32)name.count, name.data);
		}
	}
	builder.AppendUnformatted("\n"_sl);
	
	cpp_builder.Unindent();
	cpp_builder.Append("};\n\n");
}

static void GenerateCodeForRenderPass(StackAllocator* alloc, HashTable<String, HlslFileData>& hlsl_bindings_files, HlslFileData& root_signature_file, TypeInfo* type_info, Meta::RenderPass* render_pass_note) {
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	auto render_pass_name = ExtractNameWithoutNamespace(type_info_struct->name);
	auto filename = StringFormat(alloc, "%.*s.hlsl", (s32)render_pass_name.count, render_pass_name.data);
	
	auto& hlsl_file = AddOrFindHlslFile(alloc, hlsl_bindings_files, filename);
	
	GenerateCodeForRenderPass(alloc, hlsl_file, root_signature_file, type_info);
}

s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	HashTable<String, HlslFileData> hlsl_files;
	HashTable<String, HlslFileData> hlsl_bindings_files;
	
	HlslFileData root_signature_file;
	root_signature_file.builder.alloc = &alloc;
	root_signature_file.builder.AppendUnformatted("#include \"Basic/Basic.h\"\n"_sl);
	root_signature_file.builder.AppendUnformatted("#include \"Engine/RenderPasses.h\"\n\n"_sl);
	
	extern ArrayView<TypeInfo*> type_table;
	for (auto* type_info : type_table) {
		if (auto* note = FindNote<Meta::HlslFile>(type_info)) {
			GenerateCodeForHlslFile(&alloc, hlsl_files, type_info, note);
		}
		
		if (auto* note = FindNote<Meta::RenderPass>(type_info)) {
			GenerateCodeForRenderPass(&alloc, hlsl_bindings_files, root_signature_file, type_info, note);
		}
	}
	
	WriteHlslFilesToDisk(&alloc, hlsl_files);
	WriteHlslFilesToDisk(&alloc, hlsl_bindings_files);
	
	{
		SystemCreateDirectory(&alloc, "./Engine/Generated/"_sl);
		
		auto output_filepath = "./Engine/Generated/RootSignature.cpp"_sl;
		auto output_file = SystemOpenFile(&alloc, output_filepath, OpenFileFlags::Write);
		if (output_file.handle == nullptr) {
			SystemWriteToConsole(&alloc, "Failed to open output file '%s'.\n", output_filepath.data);
			SystemExitProcess(1);
		}
		
		auto file_string = root_signature_file.builder.ToString();
		SystemWriteFile(output_file, file_string.data, file_string.count, 0);
		SystemCloseFile(output_file);
	}
	
	return 0;
}

