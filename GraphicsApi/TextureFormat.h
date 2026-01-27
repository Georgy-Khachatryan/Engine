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
	R8G8B8A8_UNORM_SRGB   = 13,
	R8G8B8A8_SNORM        = 14,
	R8G8B8A8_UINT         = 15,
	R8G8B8A8_SINT         = 16,
	
	R16_TYPELESS          = 17,
	R16_UNORM             = 18,
	R16_SNORM             = 19,
	R16_UINT              = 20,
	R16_SINT              = 21,
	R16_FLOAT             = 22,
	
	R16G16_TYPELESS       = 23,
	R16G16_UNORM          = 24,
	R16G16_SNORM          = 25,
	R16G16_UINT           = 26,
	R16G16_SINT           = 27,
	R16G16_FLOAT          = 28,
	
	R16G16B16A16_TYPELESS = 29,
	R16G16B16A16_UNORM    = 30,
	R16G16B16A16_SNORM    = 31,
	R16G16B16A16_UINT     = 32,
	R16G16B16A16_SINT     = 33,
	R16G16B16A16_FLOAT    = 34,
	
	R32_TYPELESS          = 35,
	R32_UINT              = 36,
	R32_SINT              = 37,
	R32_FLOAT             = 38,
	
	R32G32_TYPELESS       = 39,
	R32G32_UINT           = 40,
	R32G32_SINT           = 41,
	R32G32_FLOAT          = 42,
	
	R32G32B32_TYPELESS    = 43,
	R32G32B32_UINT        = 44,
	R32G32B32_SINT        = 45,
	R32G32B32_FLOAT       = 46,
	
	R32G32B32A32_TYPELESS = 47,
	R32G32B32A32_UINT     = 48,
	R32G32B32A32_SINT     = 49,
	R32G32B32A32_FLOAT    = 50,
	
	R10G10B10A2_TYPELESS  = 51,
	R10G10B10A2_UNORM     = 52,
	R10G10B10A2_UINT      = 53,
	
	R11G11B10_FLOAT       = 54,
	R9G9B9E5_FLOAT        = 55,
	
	BC1_TYPELESS          = 56,
	BC1_UNORM             = 57,
	BC1_UNORM_SRGB        = 58,
	
	BC2_TYPELESS          = 59,
	BC2_UNORM             = 60,
	BC2_UNORM_SRGB        = 61,
	
	BC3_TYPELESS          = 62,
	BC3_UNORM             = 63,
	BC3_UNORM_SRGB        = 64,
	
	BC4_TYPELESS          = 65,
	BC4_UNORM             = 66,
	BC4_SNORM             = 67,
	
	BC5_TYPELESS          = 68,
	BC5_UNORM             = 69,
	BC5_SNORM             = 70,
	
	BC6H_TYPELESS         = 71,
	BC6H_UFLOAT           = 72,
	BC6H_SFLOAT           = 73,
	
	BC7_TYPELESS          = 74,
	BC7_UNORM             = 75,
	BC7_UNORM_SRGB        = 76,
	
	D16_UNORM             = 77,
	D32_FLOAT             = 78,
	D32_FLOAT_S8          = 79,
	
	R32_FLOAT_X8_TYPELESS = 80,
	X32_TYPELESS_G8_UINT  = 81,
	
	Count
};

enum struct TextureFormatFlags : u32 {
	None         = 0,
	Depth        = 1u << 0,
	Stencil      = 1u << 1,
	DepthStencil = Depth | Stencil,
};
ENUM_FLAGS_OPERATORS(TextureFormatFlags);

struct TextureFormatInfo {
	TextureFormatFlags flags = TextureFormatFlags::None;
	u32  block_size_bytes = 0;
	uint2 block_size_log2 = 0;
};

TextureFormat ToNonSrgbFormat(TextureFormat format);
TextureFormat ToSrvFormat(TextureFormat format);

enum DXGI_FORMAT;
extern ArrayView<DXGI_FORMAT> dxgi_texture_format_map;
extern ArrayView<TextureFormatInfo> texture_format_info_map;

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

