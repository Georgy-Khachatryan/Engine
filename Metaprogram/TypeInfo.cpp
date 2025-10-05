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

#define VECTOR_FIELDS_2(type) static TypeInfoStructField struct_fields_vec2##type[2] = { { "x"_sl, &type_info_##type, 0 }, { "y"_sl, &type_info_##type, sizeof(type) } }
#define VECTOR_FIELDS_3(type) static TypeInfoStructField struct_fields_vec3##type[3] = { { "x"_sl, &type_info_##type, 0 }, { "y"_sl, &type_info_##type, sizeof(type) }, { "z"_sl, &type_info_##type, sizeof(type) * 2 } }
#define VECTOR_FIELDS_4(type) static TypeInfoStructField struct_fields_vec4##type[4] = { { "x"_sl, &type_info_##type, 0 }, { "y"_sl, &type_info_##type, sizeof(type) }, { "z"_sl, &type_info_##type, sizeof(type) * 2 }, { "w"_sl, &type_info_##type, sizeof(type) * 3 } }

VECTOR_FIELDS_2(float32);
VECTOR_FIELDS_3(float32);
VECTOR_FIELDS_4(float32);
VECTOR_FIELDS_2(u32);
VECTOR_FIELDS_3(u32);
VECTOR_FIELDS_4(u32);

TypeInfoStruct type_info_float2   = { TypeInfoType::Struct, "float2"_sl,   sizeof(float2),  { struct_fields_vec2float32, 2 } };
TypeInfoStruct type_info_float3   = { TypeInfoType::Struct, "float3"_sl,   sizeof(float3),  { struct_fields_vec3float32, 3 } };
TypeInfoStruct type_info_float4   = { TypeInfoType::Struct, "float4"_sl,   sizeof(float4),  { struct_fields_vec4float32, 4 } };
TypeInfoStruct type_info_uint2    = { TypeInfoType::Struct, "uint2"_sl,    sizeof(uint2),   { struct_fields_vec2u32,     2 } };
TypeInfoStruct type_info_uint3    = { TypeInfoType::Struct, "uint3"_sl,    sizeof(uint3),   { struct_fields_vec3u32,     3 } };
TypeInfoStruct type_info_uint4    = { TypeInfoType::Struct, "uint4"_sl,    sizeof(uint4),   { struct_fields_vec4u32,     4 } };
TypeInfoStruct type_info_float4x4 = { TypeInfoType::Struct, "float4x4"_sl, sizeof(float4x4) };
TypeInfoStruct type_info_float3x4 = { TypeInfoType::Struct, "float3x4"_sl, sizeof(float3x4) };
TypeInfoStruct type_info_float3x3 = { TypeInfoType::Struct, "float3x3"_sl, sizeof(float3x3) };


String type_info_type_names[] = {
	"None"_sl,
	"Integer"_sl,
	"Float"_sl,
	"Struct"_sl,
	"Enum"_sl,
	"Type"_sl,
	"Void"_sl,
	"String"_sl,
};
static_assert(ArraySize(type_info_type_names) == (u32)TypeInfoType::Count);

