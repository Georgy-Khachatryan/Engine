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

TypeInfoFloat type_info_float16 = { TypeInfoType::Float, 16 };
TypeInfoFloat type_info_float32 = { TypeInfoType::Float, 32 };
TypeInfoFloat type_info_float64 = { TypeInfoType::Float, 64 };

TypeInfo type_info_type   = { TypeInfoType::Type   };
TypeInfo type_info_void   = { TypeInfoType::Void   };
TypeInfo type_info_string = { TypeInfoType::String };

#define VECTOR_FIELDS(type) static TypeInfoStructField struct_fields_vector_of_##type[4] = { { "x"_sl,  0, &type_info_##type, 0 }, { "y"_sl,  0, &type_info_##type, sizeof(type) }, { "z"_sl,  0, &type_info_##type, sizeof(type) * 2 }, { "w"_sl,  0, &type_info_##type, sizeof(type) * 3 } }
#define MATRIX_FIELDS(type) static TypeInfoStructField struct_fields_matrix_of_##type[4] = { { "r0"_sl, 0, &type_info_##type, 0 }, { "r1"_sl, 0, &type_info_##type, sizeof(type) }, { "r2"_sl, 0, &type_info_##type, sizeof(type) * 2 }, { "r3"_sl, 0, &type_info_##type, sizeof(type) * 3 } }

VECTOR_FIELDS(float32);
VECTOR_FIELDS(float16);
VECTOR_FIELDS(u32);
VECTOR_FIELDS(u16);
VECTOR_FIELDS(u8);
VECTOR_FIELDS(s32);
VECTOR_FIELDS(s16);
VECTOR_FIELDS(s8);
MATRIX_FIELDS(float3);
MATRIX_FIELDS(float4);

TypeInfoStruct type_info_float2    = { TypeInfoType::Struct, "float2"_sl,    0, sizeof(float2),    { struct_fields_vector_of_float32, 2 } };
TypeInfoStruct type_info_float3    = { TypeInfoType::Struct, "float3"_sl,    0, sizeof(float3),    { struct_fields_vector_of_float32, 3 } };
TypeInfoStruct type_info_float4    = { TypeInfoType::Struct, "float4"_sl,    0, sizeof(float4),    { struct_fields_vector_of_float32, 4 } };
TypeInfoStruct type_info_float16x2 = { TypeInfoType::Struct, "float16x2"_sl, 0, sizeof(float16x2), { struct_fields_vector_of_float16, 2 } };
TypeInfoStruct type_info_float16x3 = { TypeInfoType::Struct, "float16x3"_sl, 0, sizeof(float16x3), { struct_fields_vector_of_float16, 3 } };
TypeInfoStruct type_info_float16x4 = { TypeInfoType::Struct, "float16x4"_sl, 0, sizeof(float16x4), { struct_fields_vector_of_float16, 4 } };

TypeInfoStruct type_info_uint2    = { TypeInfoType::Struct, "uint2"_sl,    0, sizeof(uint2),    { struct_fields_vector_of_u32,     2 } };
TypeInfoStruct type_info_uint3    = { TypeInfoType::Struct, "uint3"_sl,    0, sizeof(uint3),    { struct_fields_vector_of_u32,     3 } };
TypeInfoStruct type_info_uint4    = { TypeInfoType::Struct, "uint4"_sl,    0, sizeof(uint4),    { struct_fields_vector_of_u32,     4 } };
TypeInfoStruct type_info_u16x2    = { TypeInfoType::Struct, "u16x2"_sl,    0, sizeof(u16x2),    { struct_fields_vector_of_u16,     2 } };
TypeInfoStruct type_info_u16x3    = { TypeInfoType::Struct, "u16x3"_sl,    0, sizeof(u16x3),    { struct_fields_vector_of_u16,     3 } };
TypeInfoStruct type_info_u16x4    = { TypeInfoType::Struct, "u16x4"_sl,    0, sizeof(u16x4),    { struct_fields_vector_of_u16,     4 } };
TypeInfoStruct type_info_u8x2     = { TypeInfoType::Struct, "u8x2"_sl,     0, sizeof(u8x2),     { struct_fields_vector_of_u8,      2 } };
TypeInfoStruct type_info_u8x3     = { TypeInfoType::Struct, "u8x3"_sl,     0, sizeof(u8x3),     { struct_fields_vector_of_u8,      3 } };
TypeInfoStruct type_info_u8x4     = { TypeInfoType::Struct, "u8x4"_sl,     0, sizeof(u8x4),     { struct_fields_vector_of_u8,      4 } };

TypeInfoStruct type_info_s32x2    = { TypeInfoType::Struct, "s32x2"_sl,    0, sizeof(s32x2),    { struct_fields_vector_of_s32,     2 } };
TypeInfoStruct type_info_s32x3    = { TypeInfoType::Struct, "s32x3"_sl,    0, sizeof(s32x3),    { struct_fields_vector_of_s32,     3 } };
TypeInfoStruct type_info_s32x4    = { TypeInfoType::Struct, "s32x4"_sl,    0, sizeof(s32x4),    { struct_fields_vector_of_s32,     4 } };
TypeInfoStruct type_info_s16x2    = { TypeInfoType::Struct, "s16x2"_sl,    0, sizeof(s16x2),    { struct_fields_vector_of_s16,     2 } };
TypeInfoStruct type_info_s16x3    = { TypeInfoType::Struct, "s16x3"_sl,    0, sizeof(s16x3),    { struct_fields_vector_of_s16,     3 } };
TypeInfoStruct type_info_s16x4    = { TypeInfoType::Struct, "s16x4"_sl,    0, sizeof(s16x4),    { struct_fields_vector_of_s16,     4 } };
TypeInfoStruct type_info_s8x2     = { TypeInfoType::Struct, "s8x2"_sl,     0, sizeof(s8x2),     { struct_fields_vector_of_s8,      2 } };
TypeInfoStruct type_info_s8x3     = { TypeInfoType::Struct, "s8x3"_sl,     0, sizeof(s8x3),     { struct_fields_vector_of_s8,      3 } };
TypeInfoStruct type_info_s8x4     = { TypeInfoType::Struct, "s8x4"_sl,     0, sizeof(s8x4),     { struct_fields_vector_of_s8,      4 } };

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
	"Array"_sl,
};
static_assert(ArraySize(type_info_type_names) == (u32)TypeInfoType::Count);

String type_info_array_type_names[] = {
	"Array"_sl,
	"FixedCapacityArray"_sl,
	"FixedCountArray"_sl,
};
static_assert(ArraySize(type_info_array_type_names) == (u32)TypeInfoArrayType::Count);
