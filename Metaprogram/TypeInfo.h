#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"

enum struct TypeInfoType : u8 {
	None    = 0,
	Integer = 1,
	Float   = 2,
	Struct  = 3,
	Enum    = 4,
	Type    = 5,
	Void    = 6,
	String  = 7,
	
	Count
};
extern String type_info_type_names[];

struct TypeInfo {
	TypeInfoType info_type = TypeInfoType::None;
};

struct TypeInfoInteger : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Integer;
	
	u8   bit_width = 0;
	bool is_signed = false;
};

struct TypeInfoFloat : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Float;
	
	u8 bit_width = 0;
};

struct TypeInfoNote {
	TypeInfo* type = nullptr;
	const void* value = nullptr;
};

enum struct TypeInfoStructFieldFlags : u32 {
	None              = 0,
	TemplateParameter = 1u << 0,
};
ENUM_FLAGS_OPERATORS(TypeInfoStructFieldFlags);

struct TypeInfoStructField {
	String name;
	
	TypeInfo* type = nullptr;
	u64 offset = 0;
	
	const void* constant_value = nullptr;
	
	TypeInfoStructFieldFlags flags = TypeInfoStructFieldFlags::None;
};

struct TypeInfoStruct : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Struct;
	
	String name;
	u64 size = 0;
	
	ArrayView<TypeInfoStructField> fields;
	ArrayView<TypeInfoNote> notes;
};

struct TypeInfoEnumField {
	String name;
	u64 value = 0;
};

struct TypeInfoEnum : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Enum;
	
	String name;
	TypeInfoInteger* underlying_type = nullptr;
	
	ArrayView<TypeInfoEnumField> fields;
	ArrayView<TypeInfoNote> notes;
};


extern TypeInfoInteger type_info_s8;
extern TypeInfoInteger type_info_s16;
extern TypeInfoInteger type_info_s32;
extern TypeInfoInteger type_info_s64;

extern TypeInfoInteger type_info_u8;
extern TypeInfoInteger type_info_u16;
extern TypeInfoInteger type_info_u32;
extern TypeInfoInteger type_info_u64;

extern TypeInfoInteger type_info_bool;

extern TypeInfoFloat type_info_float32;
extern TypeInfoFloat type_info_float64;

extern TypeInfo type_info_type;
extern TypeInfo type_info_void;
extern TypeInfo type_info_string;

extern TypeInfoStruct type_info_float2;
extern TypeInfoStruct type_info_float3;
extern TypeInfoStruct type_info_float4;
extern TypeInfoStruct type_info_uint2;
extern TypeInfoStruct type_info_uint3;
extern TypeInfoStruct type_info_uint4;
extern TypeInfoStruct type_info_float4x4;
extern TypeInfoStruct type_info_float3x4;
extern TypeInfoStruct type_info_float3x3;


template<typename T>
struct TypeInfoOfInternal { static TypeInfo* Get() { return nullptr; } };

template<typename T>
inline auto TypeInfoOf() { return TypeInfoOfInternal<const T>::Get(); }

template<> struct TypeInfoOfInternal<const s8>  { static TypeInfoInteger* Get() { return &type_info_s8;  } };
template<> struct TypeInfoOfInternal<const s16> { static TypeInfoInteger* Get() { return &type_info_s16; } };
template<> struct TypeInfoOfInternal<const s32> { static TypeInfoInteger* Get() { return &type_info_s32; } };
template<> struct TypeInfoOfInternal<const s64> { static TypeInfoInteger* Get() { return &type_info_s64; } };

template<> struct TypeInfoOfInternal<const u8>  { static TypeInfoInteger* Get() { return &type_info_u8;  } };
template<> struct TypeInfoOfInternal<const u16> { static TypeInfoInteger* Get() { return &type_info_u16; } };
template<> struct TypeInfoOfInternal<const u32> { static TypeInfoInteger* Get() { return &type_info_u32; } };
template<> struct TypeInfoOfInternal<const u64> { static TypeInfoInteger* Get() { return &type_info_u64; } };

template<> struct TypeInfoOfInternal<const bool>   { static TypeInfoInteger* Get() { return &type_info_bool;    } };
template<> struct TypeInfoOfInternal<const float>  { static TypeInfoFloat*   Get() { return &type_info_float32; } };
template<> struct TypeInfoOfInternal<const double> { static TypeInfoFloat*   Get() { return &type_info_float64; } };
template<> struct TypeInfoOfInternal<const String> { static TypeInfo*        Get() { return &type_info_string;  } };

namespace Math { struct Vec2f; struct Vec3f; struct Vec4f; struct Vec2u32; struct Vec3u32; struct Vec4u32; struct Mat4x4f; struct Mat3x4f; struct Mat3x3f; }
template<> struct TypeInfoOfInternal<const Math::Vec2f>   { static TypeInfoStruct* Get() { return &type_info_float2;   } };
template<> struct TypeInfoOfInternal<const Math::Vec3f>   { static TypeInfoStruct* Get() { return &type_info_float3;   } };
template<> struct TypeInfoOfInternal<const Math::Vec4f>   { static TypeInfoStruct* Get() { return &type_info_float4;   } };
template<> struct TypeInfoOfInternal<const Math::Vec2u32> { static TypeInfoStruct* Get() { return &type_info_uint2;    } };
template<> struct TypeInfoOfInternal<const Math::Vec3u32> { static TypeInfoStruct* Get() { return &type_info_uint3;    } };
template<> struct TypeInfoOfInternal<const Math::Vec4u32> { static TypeInfoStruct* Get() { return &type_info_uint4;    } };
template<> struct TypeInfoOfInternal<const Math::Mat4x4f> { static TypeInfoStruct* Get() { return &type_info_float4x4; } };
template<> struct TypeInfoOfInternal<const Math::Mat3x4f> { static TypeInfoStruct* Get() { return &type_info_float3x4; } };
template<> struct TypeInfoOfInternal<const Math::Mat3x3f> { static TypeInfoStruct* Get() { return &type_info_float3x3; } };


#define FORWARD_DECLARE_NOTE(name) template<> struct TypeInfoOfInternal<const name> { static TypeInfoStruct* Get(); };
