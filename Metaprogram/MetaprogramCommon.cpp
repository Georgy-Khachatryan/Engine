#include "MetaprogramCommon.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicFiles.h"


TypeInfo* ExtractTemplateParameterType(TypeInfo* type_info, u32 index) {
	if (type_info == nullptr) return nullptr;
	if (type_info->info_type != TypeInfoType::Struct) return nullptr;
	
	auto* type_info_struct = (TypeInfoStruct*)type_info;
	if (index >= type_info_struct->fields.count) return nullptr;
	
	auto& field = type_info_struct->fields[index];
	if (field.type != &type_info_type) return nullptr;
	if (HasAnyFlags(field.flags, TypeInfoStructFieldFlags::TemplateParameter) == false) return nullptr;
	
	return (TypeInfo*)field.constant_value;
}

String ExtractNameWithoutNamespace(String name) {
	u64 offset = name.count;
	while (offset != 0 && name[offset - 1] != ':') {
		offset -= 1;
	}
	
	name.data  += offset;
	name.count -= offset;
	
	return name;
}


u64 ComputeTypeSize(TypeInfo* type_info) {
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
	} case TypeInfoType::Pointer: {
		return sizeof(void*);
	} default: {
		DebugAssertAlways("Unhandled TypeInfoType.");
		return 0;
	}
	}
}

u64 ReadIntegerU64(TypeInfoInteger* type_info, const void* value) {
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


String PrintTypeName(StackAllocator* alloc, TypeInfo* type_info) {
	switch (type_info ? type_info->info_type : TypeInfoType::None) {
	case TypeInfoType::Integer: {
		auto* type_info_integer = (TypeInfoInteger*)type_info;
		switch (type_info_integer->bit_width) {
		case 8:  return type_info_integer->is_signed ? "s8"_sl  : "u8"_sl;
		case 16: return type_info_integer->is_signed ? "s16"_sl : "u16"_sl;
		case 32: return type_info_integer->is_signed ? "s32"_sl : "u32"_sl;
		case 64: return type_info_integer->is_signed ? "s64"_sl : "u64"_sl;
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
	} case TypeInfoType::Pointer: {
		auto* type_info_pointer = (TypeInfoPointer*)type_info;
		auto pointer_to_name = PrintTypeName(alloc, type_info_pointer->pointer_to);
		return StringFormat(alloc, "%.*s*", (s32)pointer_to_name.count, pointer_to_name.data);
	} case TypeInfoType::None: {
		return "None"_sl;
	} default: {
		DebugAssertAlways("Unhandled TypeInfoType.");
		return "Unknown Type"_sl;
	}
	}
}

String PrintTypeValue(StackAllocator* alloc, TypeInfo* type_info, const void* value) {
	switch (type_info ? type_info->info_type : TypeInfoType::None) {
	case TypeInfoType::Integer: {
		auto* type_info_integer = (TypeInfoInteger*)type_info;
		switch (type_info_integer->bit_width) {
		case 1:  return *(bool*)value ? "true"_sl : "false"_sl;
		case 8:  return type_info_integer->is_signed ? StringFormat(alloc, "%d",   *(s8*)value)  : StringFormat(alloc, "%u",   *(u8*)value);
		case 16: return type_info_integer->is_signed ? StringFormat(alloc, "%d",   *(s16*)value) : StringFormat(alloc, "%u",   *(u16*)value);
		case 32: return type_info_integer->is_signed ? StringFormat(alloc, "%d",   *(s32*)value) : StringFormat(alloc, "%u",   *(u32*)value);
		case 64: return type_info_integer->is_signed ? StringFormat(alloc, "%lld", *(s64*)value) : StringFormat(alloc, "%llu", *(u64*)value);
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
		u64 enum_value = ReadIntegerU64(type_info_enum->underlying_type, value);
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
	} case TypeInfoType::Pointer: {
		return StringFormat(alloc, "0x%P", value);
	} default: {
		DebugAssertAlways("Unhandled TypeInfoType.");
		return "Unknown Type"_sl;
	}
	}
}


void WriteGeneratedFile(StackAllocator* alloc, String filepath, String contents) {
	TempAllocationScope(alloc);
	
	// Don't overwrite files with the same contents to help out incremental builds.
	if (contents == SystemReadFileToString(alloc, filepath)) {
		SystemWriteToConsole(alloc, "Cached file: %.*s\n", (s32)filepath.count, filepath.data);
		return;
	}
	
	auto output_file = SystemOpenFile(alloc, filepath, OpenFileFlags::Write);
	if (output_file.handle == nullptr) {
		SystemWriteToConsole(alloc, "Failed to open output file '%.*s'.\n", (s32)filepath.count, filepath.data);
		SystemExitProcess(1);
	}
	
	SystemWriteToConsole(alloc, "Writing file: %.*s\n", (s32)filepath.count, filepath.data);
	
	SystemWriteFile(output_file, contents.data, contents.count, 0);
	SystemCloseFile(output_file);
}

void EnsureDirectoryExists(StackAllocator* alloc, String directory) {
	if (SystemCreateDirectory(alloc, directory) == false) {
		SystemWriteToConsole(alloc, "Failed to create output directory '%.*s'.\n", (s32)directory.count, directory.data);
		SystemExitProcess(1);
	}
}
