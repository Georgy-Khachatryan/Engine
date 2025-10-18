#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "TypeInfo.h"

#pragma comment(lib, "d3d12.lib")

#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>

template<typename T>
inline T* TypeInfoCast(TypeInfo* type_info) {
	return type_info && type_info->info_type == T::my_type ? (T*)type_info : nullptr;
}

template<typename T>
static T* FindNote(ArrayView<TypeInfoNote> notes) {
	auto* type_info = TypeInfoOf<T>();
	if (type_info == nullptr) return nullptr;
	
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
	case TypeInfoType::Enum:   return FindNote<T>(static_cast<TypeInfoEnum*>(type_info)->notes);
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

struct RootSignaturePassData {
	ArrayView<u8> blob;
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

static u32 ComputeTypeSize(TypeInfo* type_info) {
	switch (type_info ? type_info->info_type : TypeInfoType::None) {
	case TypeInfoType::Integer: {
		auto* type_info_integer = (TypeInfoInteger*)type_info;
		return DivideAndRoundUp(type_info_integer->bit_width, 8u);
	} case TypeInfoType::Float: {
		auto* type_info_float = (TypeInfoFloat*)type_info;
		return DivideAndRoundUp(type_info_float->bit_width, 8u);
	} case TypeInfoType::Struct: {
		auto* type_info_struct = (TypeInfoStruct*)type_info;
		return (u32)type_info_struct->size;
	} case TypeInfoType::Enum: {
		auto* type_info_enum = (TypeInfoEnum*)type_info;
		return ComputeTypeSize(type_info_enum->underlying_type);
	} case TypeInfoType::String: {
		return sizeof(String);
	} default: {
		DebugAssertAlways("Unhandled TypeInfoType.");
		return 0;
	}
	}
}

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
	} case TypeInfoType::Enum: {
		auto* type_info_enum = (TypeInfoEnum*)type_info;
		return type_info_enum->name;
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

static u64 ReadIntegerAsBits(TypeInfoInteger* type_info, const void* value) {
	switch (type_info->bit_width) {
	case 1:  return *(bool*)value;
	case 8:  return *(u8*)value;
	case 16: return *(u16*)value;
	case 32: return *(u32*)value;
	case 64: return *(u64*)value;
	}
	
	DebugAssertAlways("Unknown TypeInfoInteger bit_width '%u'.", (u32)type_info->bit_width);
	return 0;
}

static String PrintTypeValue(StackAllocator* alloc, TypeInfo* type_info, const void* value) {
	switch (type_info ? type_info->info_type : TypeInfoType::None) {
	case TypeInfoType::Integer: {
		auto* type_info_integer = (TypeInfoInteger*)type_info;
		compile_const u32 is_signed_flag = 128;
		switch (type_info_integer->bit_width | (type_info_integer->is_signed ? is_signed_flag : 0)) {
		case 1:  return *(bool*)value ? "true"_sl : "false"_sl;
		case 8:  return StringFormat(alloc, "%u", (u32)(*(u8*)value));
		case 16: return StringFormat(alloc, "%u", (u32)(*(u16*)value));
		case 32: return StringFormat(alloc, "%u",   *(u32*)value);
		case 64: return StringFormat(alloc, "%llu", *(u64*)value);
		case 8  | is_signed_flag: StringFormat(alloc, "%d", (s32)(*(s8*)value));
		case 16 | is_signed_flag: StringFormat(alloc, "%d", (s32)(*(s16*)value));
		case 32 | is_signed_flag: StringFormat(alloc, "%d",   *(s32*)value);
		case 64 | is_signed_flag: StringFormat(alloc, "%lld", *(s64*)value);
		default: return "Unknown Integer"_sl;
		}
	} case TypeInfoType::Float: {
		auto* type_info_float = (TypeInfoFloat*)type_info;
		switch (type_info_float->bit_width) {
		case 32: return StringFormat(alloc, "%f", *(float*)value);
		case 64: return StringFormat(alloc, "%f", *(double*)value);
		default: return "Unknown Float"_sl;
		}
	} case TypeInfoType::Struct: {
		auto* type_info_struct = (TypeInfoStruct*)type_info;
		
		StringBuilder builder;
		builder.alloc = alloc;
		builder.AppendUnformatted("{ "_sl);
		
		bool is_first_field = true;
		for (auto& field : type_info_struct->fields) {
			if (field.type == &type_info_type) continue;
			if (field.constant_value) continue;
			
			if (is_first_field == false) {
				builder.AppendUnformatted(", "_sl);
			}
			is_first_field = false;
			
			auto field_value = PrintTypeValue(alloc, field.type, (u8*)value + field.offset);
			builder.AppendUnformatted(field_value);
		}
		
		builder.AppendUnformatted(" }"_sl);
		
		return builder.ToString();
	} case TypeInfoType::Enum: {
		auto* type_info_enum = (TypeInfoEnum*)type_info;
		
		String value_name;
		
		u64 enum_value = ReadIntegerAsBits(type_info_enum->underlying_type, value);
		for (auto& field : type_info_enum->fields) {
			if (field.value == enum_value) {
				value_name = field.name;
				break;
			}
		}
		
		if (value_name.count == 0) {
			value_name = StringFormat(alloc, "(%.*s)%llu", (s32)type_info_enum->name.count, type_info_enum->name.data, value);
		} else {
			value_name = StringFormat(alloc, "%.*s::%.*s", (s32)type_info_enum->name.count, type_info_enum->name.data, (s32)value_name.count, value_name.data);
		}
		return value_name;
	} case TypeInfoType::String: {
		return *(String*)value;
	} default: {
		DebugAssertAlways("Unhandled TypeInfoType.");
		return "Unknown Type"_sl;
	}
	}
}

void GenerateCodeForHlslFile(StackAllocator* alloc, HlslFileData& hlsl_file, TypeInfo* type_info) {
	auto& builder = hlsl_file.builder;
	
	auto* type_info_struct = TypeInfoCast<TypeInfoStruct>(type_info);
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	// TODO: Dependent type includes?
	
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
			} else {
				include_guard[i] = CharToUpperCase(c);
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
	
	for (auto& [filename, hlsl_file] : hlsl_files) {
		auto& builder = hlsl_file.builder;
		builder.Append("#endif // %.*s\n", (s32)hlsl_file.include_guard.count, hlsl_file.include_guard.data);
		
		auto output_filepath = StringFormat(alloc, "./Shaders/Generated/%.*s", (s32)filename.count, filename.data);
		auto output_file = SystemOpenFile(alloc, output_filepath, OpenFileFlags::Write);
		if (output_file.handle == nullptr) {
			SystemWriteToConsole(alloc, "Failed to open output file '%s'.\n", output_filepath.data);
			SystemExitProcess(1);
		}
		
		SystemWriteToConsole(alloc, "Writing file: %.*s\n", (s32)output_filepath.count, output_filepath.data);
		
		auto file_string = builder.ToString();
		SystemWriteFile(output_file, file_string.data, file_string.count, 0);
		SystemCloseFile(output_file);
	}
}

static void GenerateCodeForRenderPass(StackAllocator* alloc, String filename, HlslFileData& hlsl_bindings_file, RootSignatureFileData& root_signature_file, TypeInfo* type_info, Meta::RenderPass* render_pass_note) {
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	auto& builder = hlsl_bindings_file.builder;
	
	auto name = ExtractNameWithoutNamespace(type_info_struct->name);
	
	HashTable<String, u32> dependent_types;
	u32 root_parameter_count = 0;
	
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
					root_parameter_count += 1;
				} else if (type_name == "HLSL::ConstantBuffer<T>"_sl) {
					if (auto* note = FindNote<Meta::HlslFile>(template_type)) {
						HashTableAddOrFind(dependent_types, alloc, note->filename, 0u);
					}
					root_parameter_count += 1;
				} else if (type_name == "HLSL::PushConstantBuffer<T>"_sl) {
					if (auto* note = FindNote<Meta::HlslFile>(template_type)) {
						HashTableAddOrFind(dependent_types, alloc, note->filename, 0u);
					}
					root_parameter_count += 1;
				}
			}
		}
	}
	DebugAssert(root_signature_type != nullptr, "RenderPass '%.*s' is missing root signature.", (s32)name.count, name.data);
	
	if (dependent_types.count != 0) {
		for (auto& [filename, dummy] : dependent_types) {
			builder.Append("#include \"%.*s\"\n", (s32)filename.count, filename.data);
		}
		builder.AppendUnformatted("\n"_sl);
	}
	
	
	auto& cpp_builder = root_signature_file.builder;
	
	cpp_builder.Append("%.*s::RootSignature %.*s::root_signature = {\n", (s32)name.count, name.data, (s32)name.count, name.data);
	cpp_builder.Indent();
	
	auto pass_type = PrintTypeValue(alloc, TypeInfoOf<CommandQueueType>(), &render_pass_note->pass_type);
	cpp_builder.Append("RootSignatureID{ %u }, %u, %.*s,\n", (u32)root_signature_file.root_signatures.count, root_parameter_count, (s32)pass_type.count, pass_type.data);
	
	
	Array<D3D12_ROOT_PARAMETER1> root_parameters; 
	
	u32 cbv_index = 0;
	u32 srv_index = 0;
	u32 uav_index = 0;
	
	u32 root_parameter_index = 0;
	for (auto& field : root_signature_type->fields) {
		auto* template_type = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
		
		auto type_name = PrintTypeName(field.type);
		if (type_name == "HLSL::DescriptorTable<T>"_sl) {
			auto& root_parameter = ArrayEmplace(root_parameters, alloc); 
			root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			
			Array<D3D12_DESCRIPTOR_RANGE1> descriptor_ranges;
			
			DebugAssert(template_type, "Template type of DescriptorTable '%.*s' in render pass '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
			
			u32 descriptor_count = 0;
			auto last_range_type = (D3D12_DESCRIPTOR_RANGE_TYPE)u32_max;
			for (auto& field : template_type->fields) {
				auto* template_type = ExtractTemplateParameterType(field.type, 0);
				
				auto* descriptor_type_note = FindNote<ResourceDescriptorType>(field.type);
				if (descriptor_type_note == nullptr) {
					DebugAssertAlways("Unexpected field '%.*s' of type '%.*s' used in a descriptor table of pass '%.*s'. Only descriptors are allowed.", (s32)field.name.count, field.name.data, (s32)type_name.count, type_name.data, (s32)name.count, name.data);
					continue;
				}
				auto descriptor_type = *descriptor_type_note;
				
				bool is_srv     = HasAnyFlags(descriptor_type, ResourceDescriptorType::AnySRV);
				auto range_type = is_srv ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
				auto type_name  = PrintTypeName(field.type);
				
				compile_const char* hlsl_descriptor_type_names[] = {
					"None",
					"Texture2D",
					"RWTexture2D",
					"StructuredBuffer",
					"RWStructuredBuffer",
					"ByteAddressBuffer",
					"RWByteAddressBuffer",
				};
				
				u32 descriptor_type_index = (u32)descriptor_type >> (u32)ResourceDescriptorType::IndexOffset;
				const char* descriptor_name = hlsl_descriptor_type_names[descriptor_type_index];
				
				u32 register_index = is_srv ? srv_index++ : uav_index++;
				char register_type = is_srv ? 't' : 'u';
				
				if (template_type != nullptr) {
					auto template_type_name = PrintTypeName(template_type);
					builder.Append("%s<%.*s> %.*s : register(%c%u);\n", descriptor_name, (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, register_type, register_index);
				} else {
					builder.Append("%s %.*s : register(%c%u);\n", descriptor_name, (s32)field.name.count, field.name.data, register_type, register_index);
				}
				
				if (last_range_type != range_type) {
					last_range_type = range_type;
					auto& descriptor_range = ArrayEmplace(descriptor_ranges, alloc);
					descriptor_range.RangeType          = range_type;
					descriptor_range.NumDescriptors     = 0;
					descriptor_range.BaseShaderRegister = register_index;
					descriptor_range.RegisterSpace      = 0;
					descriptor_range.Flags              = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
					descriptor_range.OffsetInDescriptorsFromTableStart = descriptor_count;
				}
				
				ArrayLastElement(descriptor_ranges).NumDescriptors += 1;
				descriptor_count += 1;
			}
			
			root_parameter.DescriptorTable.NumDescriptorRanges = (u32)descriptor_ranges.count;
			root_parameter.DescriptorTable.pDescriptorRanges   = descriptor_ranges.data;
			
			cpp_builder.Append("{ %u, %u },\n", root_parameter_index, descriptor_count);
			root_parameter_index += 1;
		} else if (type_name == "HLSL::ConstantBuffer<T>"_sl) {
			auto template_type_name = PrintTypeName(template_type);
			DebugAssert(template_type, "Template type of ConstantBuffer '%.*s' in render pass '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
			
			builder.Append("ConstantBuffer<%.*s> %.*s : register(b%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, cbv_index);
			
			auto& root_parameter = ArrayEmplace(root_parameters, alloc); 
			root_parameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
			root_parameter.Descriptor.ShaderRegister = cbv_index;
			root_parameter.Descriptor.RegisterSpace  = 0;
			root_parameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
			
			cbv_index += 1;
			
			cpp_builder.Append("{ %u },\n", root_parameter_index);
			root_parameter_index += 1;
		} else if (type_name == "HLSL::PushConstantBuffer<T>"_sl) {
			auto template_type_name = PrintTypeName(template_type);
			DebugAssert(template_type, "Template type of PushConstantBuffer '%.*s' in render pass '%.*s' is not reflected.", (s32)field.name.count, field.name.data, (s32)name.count, name.data);
			
			builder.Append("ConstantBuffer<%.*s> %.*s : register(b%u);\n", (s32)template_type_name.count, template_type_name.data, (s32)field.name.count, field.name.data, cbv_index);
			
			auto& root_parameter = ArrayEmplace(root_parameters, alloc); 
			root_parameter.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			root_parameter.Constants.ShaderRegister = cbv_index;
			root_parameter.Constants.RegisterSpace  = 0;
			root_parameter.Constants.Num32BitValues = DivideAndRoundUp(ComputeTypeSize(template_type), sizeof(u32));
			root_parameter.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
			
			cbv_index += 1;
			
			cpp_builder.Append("{ %u },\n", root_parameter_index);
			root_parameter_index += 1;
		} else if (field.type != &type_info_type) {
			DebugAssertAlways("Unexpected field '%.*s' of type '%.*s' used in a root signature of pass '%.*s'. Only root arguments are allowed.", (s32)field.name.count, field.name.data, (s32)type_name.count, type_name.data, (s32)name.count, name.data);
		}
	}
	builder.AppendUnformatted("\n"_sl);
	
	
	FixedCapacityArray<D3D12_STATIC_SAMPLER_DESC1, 7> sampler_descs;
	auto append_sampler = [&](D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE address_mode, u32 max_anisotropy = 0) {
		auto& desc = ArrayEmplace(sampler_descs);
		desc.Filter           = filter;
		desc.AddressU         = address_mode;
		desc.AddressV         = address_mode;
		desc.AddressW         = address_mode;
		desc.MipLODBias       = 0.f;
		desc.MaxAnisotropy    = max_anisotropy;
		desc.ComparisonFunc   = D3D12_COMPARISON_FUNC_NONE;
		desc.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		desc.MinLOD           = 0.f;
		desc.MaxLOD           = D3D12_FLOAT32_MAX;
		desc.ShaderRegister   = (u32)sampler_descs.count - 1;
		desc.RegisterSpace    = 0;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.Flags            = D3D12_SAMPLER_FLAG_NONE;
	};
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	append_sampler(D3D12_FILTER_ANISOTROPIC,        D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0);
	append_sampler(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	append_sampler(D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	
	
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs_desc = {};
	rs_desc.Version  = D3D_ROOT_SIGNATURE_VERSION_1_2;
	rs_desc.Desc_1_2.NumParameters     = (u32)root_parameters.count;
	rs_desc.Desc_1_2.pParameters       = root_parameters.data;
	rs_desc.Desc_1_2.NumStaticSamplers = (u32)sampler_descs.count;
	rs_desc.Desc_1_2.pStaticSamplers   = sampler_descs.data;
	rs_desc.Desc_1_2.Flags             = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
	
	ID3DBlob* root_signature_blob = nullptr;
	ID3DBlob* root_signature_error_blob = nullptr;
	if (FAILED(D3D12SerializeVersionedRootSignature(&rs_desc, &root_signature_blob, &root_signature_error_blob))) {
		DebugAssertAlways("Failed to serialize root signature for pass '%.*s'. Errors: %.*s", (s32)name.count, name.data, (s32)root_signature_blob->GetBufferSize(), root_signature_blob->GetBufferPointer());
	}
	
	RootSignaturePassData root_signature;
	root_signature.blob.data  = (u8*)root_signature_blob->GetBufferPointer();
	root_signature.blob.count = root_signature_blob->GetBufferSize();
	root_signature.include_file_name = filename;
	root_signature.render_pass_name  = name;
	ArrayAppend(root_signature_file.root_signatures, alloc, root_signature);
	
	cpp_builder.Unindent();
	cpp_builder.Append("};\n\n");
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
	
	WriteHlslFilesToDisk(&alloc, hlsl_files);
	WriteHlslFilesToDisk(&alloc, hlsl_bindings_files);
	
	{
		auto& builder = root_signature_file.builder;
		builder.AppendUnformatted("static String root_signature_filenames_internal[] = {\n"_sl);
		builder.Indent();
		
		for (auto& root_signature : root_signature_file.root_signatures) {
			builder.Append("\"%.*s\"_sl,\n", (s32)root_signature.include_file_name.count, root_signature.include_file_name.data);
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
		
		builder.Append("ArrayView<String> root_signature_filenames = { root_signature_filenames_internal, %u };\n\n", (u32)root_signature_file.root_signatures.count);
		
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
		
		SystemCreateDirectory(&alloc, "./Engine/Generated/"_sl);
		
		auto output_filepath = "./Engine/Generated/RootSignature.cpp"_sl;
		auto output_file = SystemOpenFile(&alloc, output_filepath, OpenFileFlags::Write);
		if (output_file.handle == nullptr) {
			SystemWriteToConsole(&alloc, "Failed to open output file '%s'.\n", output_filepath.data);
			SystemExitProcess(1);
		}
		
		auto file_string = builder.ToString();
		SystemWriteFile(output_file, file_string.data, file_string.count, 0);
		SystemCloseFile(output_file);
	}
	
	{
		auto output_filepath = "./Build/RootSignature.bin"_sl;
		auto file = SystemOpenFile(&alloc, output_filepath, OpenFileFlags::Write);
		if (file.handle == nullptr) {
			SystemWriteToConsole(&alloc, "Failed to open output file '%s'.\n", output_filepath.data);
			SystemExitProcess(1);
		}
		
		Array<u32> offsets;
		ArrayResize(offsets, &alloc, root_signature_file.root_signatures.count);
		
		u32 offset = (u32)(sizeof(u32) + offsets.count * sizeof(u32));
		for (u32 i = 0; i < offsets.count; i += 1) {
			u32 size = (u32)root_signature_file.root_signatures[i].blob.count;
			offsets[i] = offset;
			offset += size;
		}
		
		SystemWriteFile(file, &offsets.count, sizeof(u32), 0);
		SystemWriteFile(file, offsets.data, offsets.count * sizeof(u32), sizeof(u32));
		
		offset = (u32)(sizeof(u32) + offsets.count * sizeof(u32));
		for (u32 i = 0; i < offsets.count; i += 1) {
			u32 size = (u32)root_signature_file.root_signatures[i].blob.count;
			SystemWriteFile(file, root_signature_file.root_signatures[i].blob.data, size, offset);
			offset += size;
		}
		
		SystemCloseFile(file);
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
		
		auto output_filepath = "./Engine/Generated/ShaderDefinitions.cpp"_sl;
		auto output_file = SystemOpenFile(&alloc, output_filepath, OpenFileFlags::Write);
		if (output_file.handle == nullptr) {
			SystemWriteToConsole(&alloc, "Failed to open output file '%s'.\n", output_filepath.data);
			SystemExitProcess(1);
		}
		
		auto file_string = builder.ToString();
		SystemWriteFile(output_file, file_string.data, file_string.count, 0);
		SystemCloseFile(output_file);
	}
	
	return 0;
}

