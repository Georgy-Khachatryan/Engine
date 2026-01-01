#include "TypeInfo.h"
#include "Basic/BasicMath.h"

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

TypeInfo type_info_type   = { TypeInfoType::Type   };
TypeInfo type_info_void   = { TypeInfoType::Void   };
TypeInfo type_info_string = { TypeInfoType::String };

#define VECTOR_FIELDS(type) static TypeInfoStructField struct_fields_vector_of_##type[4] = { { "x"_sl,  0, &type_info_##type, 0 }, { "y"_sl,  0, &type_info_##type, sizeof(type) }, { "z"_sl,  0, &type_info_##type, sizeof(type) * 2 }, { "w"_sl,  0, &type_info_##type, sizeof(type) * 3 } }
#define MATRIX_FIELDS(type) static TypeInfoStructField struct_fields_matrix_of_##type[4] = { { "r0"_sl, 0, &type_info_##type, 0 }, { "r1"_sl, 0, &type_info_##type, sizeof(type) }, { "r2"_sl, 0, &type_info_##type, sizeof(type) * 2 }, { "r3"_sl, 0, &type_info_##type, sizeof(type) * 3 } }

VECTOR_FIELDS(float32);
VECTOR_FIELDS(u32);
MATRIX_FIELDS(float3);
MATRIX_FIELDS(float4);

TypeInfoStruct type_info_float2   = { TypeInfoType::Struct, "float2"_sl,   0, sizeof(float2),   { struct_fields_vector_of_float32, 2 } };
TypeInfoStruct type_info_float3   = { TypeInfoType::Struct, "float3"_sl,   0, sizeof(float3),   { struct_fields_vector_of_float32, 3 } };
TypeInfoStruct type_info_float4   = { TypeInfoType::Struct, "float4"_sl,   0, sizeof(float4),   { struct_fields_vector_of_float32, 4 } };
TypeInfoStruct type_info_uint2    = { TypeInfoType::Struct, "uint2"_sl,    0, sizeof(uint2),    { struct_fields_vector_of_u32,     2 } };
TypeInfoStruct type_info_uint3    = { TypeInfoType::Struct, "uint3"_sl,    0, sizeof(uint3),    { struct_fields_vector_of_u32,     3 } };
TypeInfoStruct type_info_uint4    = { TypeInfoType::Struct, "uint4"_sl,    0, sizeof(uint4),    { struct_fields_vector_of_u32,     4 } };
TypeInfoStruct type_info_quat     = { TypeInfoType::Struct, "quat"_sl,     0, sizeof(quat),     { struct_fields_vector_of_float32, 4 } };
TypeInfoStruct type_info_float4x4 = { TypeInfoType::Struct, "float4x4"_sl, 0, sizeof(float4x4), { struct_fields_matrix_of_float4,  4 } };
TypeInfoStruct type_info_float3x4 = { TypeInfoType::Struct, "float3x4"_sl, 0, sizeof(float3x4), { struct_fields_matrix_of_float4,  3 } };
TypeInfoStruct type_info_float3x3 = { TypeInfoType::Struct, "float3x3"_sl, 0, sizeof(float3x3), { struct_fields_matrix_of_float3,  3 } };


String type_info_type_names[] = {
	"None"_sl,
	"Integer"_sl,
	"Float"_sl,
	"Struct"_sl,
	"Enum"_sl,
	"Type"_sl,
	"Void"_sl,
	"String"_sl,
	"Pointer"_sl,
};
static_assert(ArraySize(type_info_type_names) == (u32)TypeInfoType::Count);

