#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Tokens.h"
#include "AstNodes.h"
#include "TypeInfo.h"


s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	extern ArrayView<TypeInfo*> type_table;
	for (auto* type_info : type_table) {
		switch (type_info->info_type) {
		case TypeInfoType::Integer: {
			auto* type_info_integer = (TypeInfoInteger*)type_info;
			SystemWriteToConsole(&alloc, "%c%u\n", type_info_integer->is_signed ? 's' : 'u', type_info_integer->bit_width);
			break;
		} case TypeInfoType::Float: {
			auto* type_info_float = (TypeInfoFloat*)type_info;
			SystemWriteToConsole(&alloc, "float%u\n", type_info_float->bit_width);
			break;
		} case TypeInfoType::Struct: {
			auto* type_info_struct = (TypeInfoStruct*)type_info;
			SystemWriteToConsole(&alloc, "%s\n", type_info_struct->name.data);
			break;
		} case TypeInfoType::Type: {
			SystemWriteToConsole(&alloc, "Type\n");
			break;
		} case TypeInfoType::Void: {
			SystemWriteToConsole(&alloc, "Void\n");
			break;
		} case TypeInfoType::String: {
			SystemWriteToConsole(&alloc, "String\n");
			break;
		}
		}
	}
	
	return 0;
}

