#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMath.h"

enum struct TextureFormat : u8 {
	None                  = 0,
	
	R8_TYPELESS           = 1,
	R8_UNORM              = 2,
	R8_SNORM              = 3,
	R8_UINT               = 4,
	R8_SINT               = 5,
	
	R8G8_TYPELESS         = 6,
	R8G8_UNORM            = 7,
	R8G8_SNORM            = 8,
	R8G8_UINT             = 9,
	R8G8_SINT             = 10,
	
	R8G8B8A8_TYPELESS     = 11,
	R8G8B8A8_UNORM        = 12,
	R8G8B8A8_SNORM        = 13,
	R8G8B8A8_UINT         = 14,
	R8G8B8A8_SINT         = 15,
	
	R16_TYPELESS          = 16,
	R16_UNORM             = 17,
	R16_SNORM             = 18,
	R16_UINT              = 19,
	R16_SINT              = 20,
	R16_FLOAT             = 21,
	
	R16G16_TYPELESS       = 22,
	R16G16_UNORM          = 23,
	R16G16_SNORM          = 24,
	R16G16_UINT           = 25,
	R16G16_SINT           = 26,
	R16G16_FLOAT          = 27,
	
	R16G16B16A16_TYPELESS = 28,
	R16G16B16A16_UNORM    = 29,
	R16G16B16A16_SNORM    = 30,
	R16G16B16A16_UINT     = 31,
	R16G16B16A16_SINT     = 32,
	R16G16B16A16_FLOAT    = 33,
	
	R32_TYPELESS          = 34,
	R32_UINT              = 35,
	R32_SINT              = 36,
	R32_FLOAT             = 37,
	
	R32G32_TYPELESS       = 38,
	R32G32_UINT           = 39,
	R32G32_SINT           = 40,
	R32G32_FLOAT          = 41,
	
	R32G32B32_TYPELESS    = 42,
	R32G32B32_UINT        = 43,
	R32G32B32_SINT        = 44,
	R32G32B32_FLOAT       = 45,
	
	R32G32B32A32_TYPELESS = 46,
	R32G32B32A32_UINT     = 47,
	R32G32B32A32_SINT     = 48,
	R32G32B32A32_FLOAT    = 49,
	
	R10G10B10A2_TYPELESS  = 50,
	R10G10B10A2_UNORM     = 51,
	R10G10B10A2_UINT      = 52,
	
	R11G11B10_FLOAT       = 53,
	R9G9B9E5_FLOAT        = 54,
	
	BC1_TYPELESS          = 55,
	BC1_UNORM             = 56,
	BC1_UNORM_SRGB        = 57,
	
	BC2_TYPELESS          = 58,
	BC2_UNORM             = 59,
	BC2_UNORM_SRGB        = 60,
	
	BC3_TYPELESS          = 61,
	BC3_UNORM             = 62,
	BC3_UNORM_SRGB        = 63,
	
	BC4_TYPELESS          = 64,
	BC4_UNORM             = 65,
	BC4_SNORM             = 66,
	
	BC5_TYPELESS          = 67,
	BC5_UNORM             = 68,
	BC5_SNORM             = 69,
	
	BC6H_TYPELESS         = 70,
	BC6H_UFLOAT           = 71,
	BC6H_SFLOAT           = 72,
	
	BC7_TYPELESS          = 73,
	BC7_UNORM             = 74,
	BC7_UNORM_SRGB        = 75,
	
	D16_UNORM             = 76,
	D32_FLOAT             = 77,
	D32_FLOAT_S8          = 78,
	X32_TYPELESS_G8       = 79,
	
	Count
};

enum DXGI_FORMAT;
extern ArrayView<DXGI_FORMAT> dxgi_texture_format_map;


struct TextureSize {
	enum struct Type : u8 {
		Texture2D   = 0,
		Texture3D   = 1,
		TextureCube = 2,
	};
	
	union {
		struct {
			u16  x;
			u16  y;
			u16  z;
			u8   mips : 5;
			Type type : 3;
			TextureFormat format;
		};
		u64 packed;
	};
	
	
	TextureSize() : x(0), y(0), z(0), mips(0), type(Type::Texture2D), format(TextureFormat::None) {}
	TextureSize(const TextureSize& other) = default;
	TextureSize(TextureFormat format, u32 x, u32 y, u32 z = 1, u32 mips = 1, Type type = Type::Texture2D)
		: x((u16)x), y((u16)y), z((u16)z), mips((u8)mips), type(type), format(format) {}
	TextureSize(TextureFormat format, uint2 xy, u32 z = 1, u32 mips = 1, Type type = Type::Texture2D)
		: x((u16)xy.x), y((u16)xy.y), z((u16)z), mips((u8)mips), type(type), format(format) {}
	
	bool operator==(const TextureSize& other) { return packed == other.packed; }
	bool operator!=(const TextureSize& other) { return packed != other.packed; }
	
	u32 ArraySliceCount() const { return type != Type::Texture3D ? z : 1; }
	u32 DepthSliceCount() const { return type == Type::Texture3D ? z : 1; }
};
static_assert(sizeof(TextureSize) == 8, "Layout of TextureSize is not valid.");

