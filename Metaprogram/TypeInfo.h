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
	Pointer = 8,
	Array   = 9,
	
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

struct TypeInfoPointer : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Pointer;
	
	TypeInfo* pointer_to = nullptr;
};

enum struct TypeInfoArrayType : u32 {
	Array              = 0,
	FixedCapacityArray = 1,
	FixedCountArray    = 2,
	
	Count
};
extern String type_info_array_type_names[];

struct TypeInfoArray : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Array;
	
	TypeInfoArrayType array_type = TypeInfoArrayType::Array;
	TypeInfo* array_of = nullptr;
	u64 fixed_size = 0; // Fixed count or capacity.
};

struct TypeInfoNote {
	TypeInfo* type = nullptr;
	const void* value = nullptr;
	u64 source_location = 0;
};

enum struct TypeInfoStructFieldFlags : u32 {
	None              = 0,
	TemplateParameter = 1u << 0,
};
ENUM_FLAGS_OPERATORS(TypeInfoStructFieldFlags);

struct TypeInfoStructField {
	String name;
	u64 source_location = 0;
	
	TypeInfo* type = nullptr;
	u64 offset = 0;
	ArrayView<TypeInfoNote> notes;
	
	const void* constant_value = nullptr;
	
	TypeInfoStructFieldFlags flags = TypeInfoStructFieldFlags::None;
};

struct TypeInfoStruct : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Struct;
	
	String name;
	u64 source_location = 0;
	
	u64 size = 0;
	ArrayView<TypeInfoStructField> fields;
	ArrayView<TypeInfoNote> notes;
};

struct TypeInfoEnumField {
	String name;
	u64 source_location = 0;
	
	u64 value = 0;
	ArrayView<TypeInfoNote> notes;
};

struct TypeInfoEnum : TypeInfo {
	compile_const TypeInfoType my_type = TypeInfoType::Enum;
	
	String name;
	u64 source_location = 0;
	
	TypeInfoInteger* underlying_type = nullptr;
	
	ArrayView<TypeInfoEnumField> fields;
	ArrayView<TypeInfoNote> notes;
};

struct TypeInfoSourceFile {
	String filepath;
	u64 hash = 0;
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

extern TypeInfoFloat type_info_float16;
extern TypeInfoFloat type_info_float32;
extern TypeInfoFloat type_info_float64;

extern TypeInfo type_info_type;
extern TypeInfo type_info_void;
extern TypeInfo type_info_string;

extern TypeInfoStruct type_info_float2;
extern TypeInfoStruct type_info_float3;
extern TypeInfoStruct type_info_float4;

extern TypeInfoStruct type_info_float16x4;
extern TypeInfoStruct type_info_float16x3;
extern TypeInfoStruct type_info_float16x2;

extern TypeInfoStruct type_info_uint2;
extern TypeInfoStruct type_info_uint3;
extern TypeInfoStruct type_info_uint4;

extern TypeInfoStruct type_info_u16x4;
extern TypeInfoStruct type_info_u16x3;
extern TypeInfoStruct type_info_u16x2;

extern TypeInfoStruct type_info_u8x4;
extern TypeInfoStruct type_info_u8x3;
extern TypeInfoStruct type_info_u8x2;

extern TypeInfoStruct type_info_s32x4;
extern TypeInfoStruct type_info_s32x3;
extern TypeInfoStruct type_info_s32x2;

extern TypeInfoStruct type_info_s16x4;
extern TypeInfoStruct type_info_s16x3;
extern TypeInfoStruct type_info_s16x2;

extern TypeInfoStruct type_info_s8x4;
extern TypeInfoStruct type_info_s8x3;
extern TypeInfoStruct type_info_s8x2;

extern TypeInfoStruct type_info_quat;
extern TypeInfoStruct type_info_float4x4;
extern TypeInfoStruct type_info_float3x4;
extern TypeInfoStruct type_info_float3x3;


template<typename T>
struct TypeInfoOfInternal { static TypeInfo* Get() { return nullptr; } };

template<typename T>
inline auto TypeInfoOf() { return TypeInfoOfInternal<const T>::Get(); }

template<typename T> struct TypeInfoOfInternal<const T* const> {
	static TypeInfoPointer* Get() {
		static TypeInfoPointer type_info = {
			TypeInfoType::Pointer,
			TypeInfoOf<T>(),
		};
		return &type_info;
	}
};

template<typename T> struct TypeInfoOfInternal<T* const> {
	static TypeInfoPointer* Get() {
		static TypeInfoPointer type_info = {
			TypeInfoType::Pointer,
			TypeInfoOf<T>(),
		};
		return &type_info;
	}
};

template<typename T> struct TypeInfoOfInternal<const Array<T>> {
	static TypeInfoArray* Get() {
		static TypeInfoArray type_info = {
			TypeInfoType::Array,
			TypeInfoArrayType::Array,
			TypeInfoOf<T>(),
		};
		return &type_info;
	}
};

template<typename T, u64 fixed_capacity> struct TypeInfoOfInternal<const FixedCapacityArray<T, fixed_capacity>> {
	static TypeInfoArray* Get() {
		static TypeInfoArray type_info = {
			TypeInfoType::Array,
			TypeInfoArrayType::FixedCapacityArray,
			TypeInfoOf<T>(),
			fixed_capacity,
		};
		return &type_info;
	}
};

template<typename T, u64 fixed_count> struct TypeInfoOfInternal<const FixedCountArray<T, fixed_count>> {
	static TypeInfoArray* Get() {
		static TypeInfoArray type_info = {
			TypeInfoType::Array,
			TypeInfoArrayType::FixedCountArray,
			TypeInfoOf<T>(),
			fixed_count,
		};
		return &type_info;
	}
};

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
template<> struct TypeInfoOfInternal<const void>   { static TypeInfo*        Get() { return &type_info_void;    } };


namespace Math { struct Vec2f; struct Vec3f; struct Vec4f; }
template<> struct TypeInfoOfInternal<const Math::Vec2f>   { static TypeInfoStruct* Get() { return &type_info_float2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3f>   { static TypeInfoStruct* Get() { return &type_info_float3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4f>   { static TypeInfoStruct* Get() { return &type_info_float4; } };

namespace Math { struct Vec2h; struct Vec3h; struct Vec4h; }
template<> struct TypeInfoOfInternal<const Math::Vec2h>   { static TypeInfoStruct* Get() { return &type_info_float16x2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3h>   { static TypeInfoStruct* Get() { return &type_info_float16x3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4h>   { static TypeInfoStruct* Get() { return &type_info_float16x4; } };

namespace Math { struct Vec2u32; struct Vec3u32; struct Vec4u32; }
template<> struct TypeInfoOfInternal<const Math::Vec2u32> { static TypeInfoStruct* Get() { return &type_info_uint2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3u32> { static TypeInfoStruct* Get() { return &type_info_uint3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4u32> { static TypeInfoStruct* Get() { return &type_info_uint4; } };

namespace Math { struct Vec2u16; struct Vec3u16; struct Vec4u16; }
template<> struct TypeInfoOfInternal<const Math::Vec2u16> { static TypeInfoStruct* Get() { return &type_info_u16x2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3u16> { static TypeInfoStruct* Get() { return &type_info_u16x3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4u16> { static TypeInfoStruct* Get() { return &type_info_u16x4; } };

namespace Math { struct Vec2u8; struct Vec3u8; struct Vec4u8; }
template<> struct TypeInfoOfInternal<const Math::Vec2u8> { static TypeInfoStruct* Get() { return &type_info_u8x2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3u8> { static TypeInfoStruct* Get() { return &type_info_u8x3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4u8> { static TypeInfoStruct* Get() { return &type_info_u8x4; } };

namespace Math { struct Vec2s32; struct Vec3s32; struct Vec4s32; }
template<> struct TypeInfoOfInternal<const Math::Vec2s32> { static TypeInfoStruct* Get() { return &type_info_s32x2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3s32> { static TypeInfoStruct* Get() { return &type_info_s32x3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4s32> { static TypeInfoStruct* Get() { return &type_info_s32x4; } };

namespace Math { struct Vec2s16; struct Vec3s16; struct Vec4s16; }
template<> struct TypeInfoOfInternal<const Math::Vec2s16> { static TypeInfoStruct* Get() { return &type_info_s16x2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3s16> { static TypeInfoStruct* Get() { return &type_info_s16x3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4s16> { static TypeInfoStruct* Get() { return &type_info_s16x4; } };

namespace Math { struct Vec2s8; struct Vec3s8; struct Vec4s8; }
template<> struct TypeInfoOfInternal<const Math::Vec2s8> { static TypeInfoStruct* Get() { return &type_info_s8x2; } };
template<> struct TypeInfoOfInternal<const Math::Vec3s8> { static TypeInfoStruct* Get() { return &type_info_s8x3; } };
template<> struct TypeInfoOfInternal<const Math::Vec4s8> { static TypeInfoStruct* Get() { return &type_info_s8x4; } };

namespace Math { struct Quatf; struct Mat4x4f; struct Mat3x4f; struct Mat3x3f; }
template<> struct TypeInfoOfInternal<const Math::Quatf>   { static TypeInfoStruct* Get() { return &type_info_quat;     } };
template<> struct TypeInfoOfInternal<const Math::Mat4x4f> { static TypeInfoStruct* Get() { return &type_info_float4x4; } };
template<> struct TypeInfoOfInternal<const Math::Mat3x4f> { static TypeInfoStruct* Get() { return &type_info_float3x4; } };
template<> struct TypeInfoOfInternal<const Math::Mat3x3f> { static TypeInfoStruct* Get() { return &type_info_float3x3; } };

