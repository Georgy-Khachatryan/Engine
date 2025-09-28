#include "TypeInfo.h"

TypeInfoInteger type_info_s8  = { TypeInfoType::Integer, 8,  true };
TypeInfoInteger type_info_s16 = { TypeInfoType::Integer, 16, true };
TypeInfoInteger type_info_s32 = { TypeInfoType::Integer, 32, true };
TypeInfoInteger type_info_s64 = { TypeInfoType::Integer, 64, true };

TypeInfoInteger type_info_u8  = { TypeInfoType::Integer, 8,  false };
TypeInfoInteger type_info_u16 = { TypeInfoType::Integer, 16, false };
TypeInfoInteger type_info_u32 = { TypeInfoType::Integer, 32, false };
TypeInfoInteger type_info_u64 = { TypeInfoType::Integer, 64, false };

TypeInfoInteger type_info_bool = { TypeInfoType::Integer, 1, false };

TypeInfoFloat type_info_float32 = { TypeInfoType::Float, 32 };
TypeInfoFloat type_info_float64 = { TypeInfoType::Float, 64 };

TypeInfo type_info_type = { TypeInfoType::Type };
TypeInfo type_info_void = { TypeInfoType::Void };
TypeInfo type_info_string = { TypeInfoType::String };


String type_info_type_names[] = {
	"None"_sl,
	"Integer"_sl,
	"Float"_sl,
	"Struct"_sl,
	"Type"_sl,
	"Void"_sl,
	"String"_sl,
};
static_assert(ArraySize(type_info_type_names) == (u32)TypeInfoType::Count);

