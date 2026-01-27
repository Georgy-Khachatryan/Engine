#include "TextureFormat.h"

#include <SDK/D3D12/include/dxgiformat.h>

TextureFormat ToNonSrgbFormat(TextureFormat format) {
	switch (format) {
	case TextureFormat::R8G8B8A8_UNORM_SRGB: return TextureFormat::R8G8B8A8_UNORM;
	case TextureFormat::BC1_UNORM_SRGB: return TextureFormat::BC1_UNORM;
	case TextureFormat::BC2_UNORM_SRGB: return TextureFormat::BC2_UNORM;
	case TextureFormat::BC3_UNORM_SRGB: return TextureFormat::BC3_UNORM;
	case TextureFormat::BC7_UNORM_SRGB: return TextureFormat::BC7_UNORM;
	default: return format;
	}
}

TextureFormat ToSrvFormat(TextureFormat format) {
	switch (format) {
	case TextureFormat::D16_UNORM: return TextureFormat::R16_UNORM;
	case TextureFormat::D32_FLOAT: return TextureFormat::R32_FLOAT;
	case TextureFormat::D32_FLOAT_S8: return TextureFormat::R32_FLOAT_X8_TYPELESS;
	default: return format;
	}
}

static DXGI_FORMAT dxgi_texture_formats[(u32)TextureFormat::Count] = {
	DXGI_FORMAT_UNKNOWN,
	
	DXGI_FORMAT_R8_TYPELESS,
	DXGI_FORMAT_R8_UNORM,
	DXGI_FORMAT_R8_SNORM,
	DXGI_FORMAT_R8_UINT,
	DXGI_FORMAT_R8_SINT,
	
	DXGI_FORMAT_R8G8_TYPELESS,
	DXGI_FORMAT_R8G8_UNORM,
	DXGI_FORMAT_R8G8_SNORM,
	DXGI_FORMAT_R8G8_UINT,
	DXGI_FORMAT_R8G8_SINT,
	
	DXGI_FORMAT_R8G8B8A8_TYPELESS,
	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
	DXGI_FORMAT_R8G8B8A8_SNORM,
	DXGI_FORMAT_R8G8B8A8_UINT,
	DXGI_FORMAT_R8G8B8A8_SINT,
	
	DXGI_FORMAT_R16_TYPELESS,
	DXGI_FORMAT_R16_UNORM,
	DXGI_FORMAT_R16_SNORM,
	DXGI_FORMAT_R16_UINT,
	DXGI_FORMAT_R16_SINT,
	DXGI_FORMAT_R16_FLOAT,
	
	DXGI_FORMAT_R16G16_TYPELESS,
	DXGI_FORMAT_R16G16_UNORM,
	DXGI_FORMAT_R16G16_SNORM,
	DXGI_FORMAT_R16G16_UINT,
	DXGI_FORMAT_R16G16_SINT,
	DXGI_FORMAT_R16G16_FLOAT,
	
	DXGI_FORMAT_R16G16B16A16_TYPELESS,
	DXGI_FORMAT_R16G16B16A16_UNORM,
	DXGI_FORMAT_R16G16B16A16_SNORM,
	DXGI_FORMAT_R16G16B16A16_UINT,
	DXGI_FORMAT_R16G16B16A16_SINT,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	
	DXGI_FORMAT_R32_TYPELESS,
	DXGI_FORMAT_R32_UINT,
	DXGI_FORMAT_R32_SINT,
	DXGI_FORMAT_R32_FLOAT,
	
	DXGI_FORMAT_R32G32_TYPELESS,
	DXGI_FORMAT_R32G32_UINT,
	DXGI_FORMAT_R32G32_SINT,
	DXGI_FORMAT_R32G32_FLOAT,
	
	DXGI_FORMAT_R32G32B32_TYPELESS,
	DXGI_FORMAT_R32G32B32_UINT,
	DXGI_FORMAT_R32G32B32_SINT,
	DXGI_FORMAT_R32G32B32_FLOAT,
	
	DXGI_FORMAT_R32G32B32A32_TYPELESS,
	DXGI_FORMAT_R32G32B32A32_UINT,
	DXGI_FORMAT_R32G32B32A32_SINT,
	DXGI_FORMAT_R32G32B32A32_FLOAT,
	
	DXGI_FORMAT_R10G10B10A2_TYPELESS,
	DXGI_FORMAT_R10G10B10A2_UNORM,
	DXGI_FORMAT_R10G10B10A2_UINT,
	
	DXGI_FORMAT_R11G11B10_FLOAT,
	DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
	
	DXGI_FORMAT_BC1_TYPELESS,
	DXGI_FORMAT_BC1_UNORM,
	DXGI_FORMAT_BC1_UNORM_SRGB,
	
	DXGI_FORMAT_BC2_TYPELESS,
	DXGI_FORMAT_BC2_UNORM,
	DXGI_FORMAT_BC2_UNORM_SRGB,
	
	DXGI_FORMAT_BC3_TYPELESS,
	DXGI_FORMAT_BC3_UNORM,
	DXGI_FORMAT_BC3_UNORM_SRGB,
	
	DXGI_FORMAT_BC4_TYPELESS,
	DXGI_FORMAT_BC4_UNORM,
	DXGI_FORMAT_BC4_SNORM,
	
	DXGI_FORMAT_BC5_TYPELESS,
	DXGI_FORMAT_BC5_UNORM,
	DXGI_FORMAT_BC5_SNORM,
	
	DXGI_FORMAT_BC6H_TYPELESS,
	DXGI_FORMAT_BC6H_UF16,
	DXGI_FORMAT_BC6H_SF16,
	
	DXGI_FORMAT_BC7_TYPELESS,
	DXGI_FORMAT_BC7_UNORM,
	DXGI_FORMAT_BC7_UNORM_SRGB,
	
	DXGI_FORMAT_D16_UNORM,
	DXGI_FORMAT_D32_FLOAT,
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
	
	DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
	DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
};

ArrayView<DXGI_FORMAT> dxgi_texture_format_map = { dxgi_texture_formats, (u64)TextureFormat::Count };


static TextureFormatInfo texture_format_infos[(u32)TextureFormat::Count] = {
	{ TextureFormatFlags::None, 0, uint2(0, 0) }, // None
	
	{ TextureFormatFlags::None, 1, uint2(0, 0) }, // R8_TYPELESS
	{ TextureFormatFlags::None, 1, uint2(0, 0) }, // R8_UNORM
	{ TextureFormatFlags::None, 1, uint2(0, 0) }, // R8_SNORM
	{ TextureFormatFlags::None, 1, uint2(0, 0) }, // R8_UINT
	{ TextureFormatFlags::None, 1, uint2(0, 0) }, // R8_SINT
	
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R8G8_TYPELESS
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R8G8_UNORM
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R8G8_SNORM
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R8G8_UINT
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R8G8_SINT
	
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R8G8B8A8_TYPELESS
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R8G8B8A8_UNORM
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R8G8B8A8_UNORM_SRGB
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R8G8B8A8_SNORM
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R8G8B8A8_UINT
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R8G8B8A8_SINT
	
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R16_TYPELESS
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R16_UNORM
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R16_SNORM
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R16_UINT
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R16_SINT
	{ TextureFormatFlags::None, 2, uint2(0, 0) }, // R16_FLOAT
	
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R16G16_TYPELESS
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R16G16_UNORM
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R16G16_SNORM
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R16G16_UINT
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R16G16_SINT
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R16G16_FLOAT
	
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R16G16B16A16_TYPELESS
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R16G16B16A16_UNORM
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R16G16B16A16_SNORM
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R16G16B16A16_UINT
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R16G16B16A16_SINT
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R16G16B16A16_FLOAT
	
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R32_TYPELESS
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R32_UINT
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R32_SINT
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R32_FLOAT
	
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R32G32_TYPELESS
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R32G32_UINT
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R32G32_SINT
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R32G32_FLOAT
	
	{ TextureFormatFlags::None, 12, uint2(0, 0) }, // R32G32B32_TYPELESS
	{ TextureFormatFlags::None, 12, uint2(0, 0) }, // R32G32B32_UINT
	{ TextureFormatFlags::None, 12, uint2(0, 0) }, // R32G32B32_SINT
	{ TextureFormatFlags::None, 12, uint2(0, 0) }, // R32G32B32_FLOAT
	
	{ TextureFormatFlags::None, 16, uint2(0, 0) }, // R32G32B32A32_TYPELESS
	{ TextureFormatFlags::None, 16, uint2(0, 0) }, // R32G32B32A32_UINT
	{ TextureFormatFlags::None, 16, uint2(0, 0) }, // R32G32B32A32_SINT
	{ TextureFormatFlags::None, 16, uint2(0, 0) }, // R32G32B32A32_FLOAT
	
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R10G10B10A2_TYPELESS
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R10G10B10A2_UNORM
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R10G10B10A2_UINT
	
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R11G11B10_FLOAT
	{ TextureFormatFlags::None, 4, uint2(0, 0) }, // R9G9B9E5_FLOAT
	
	{ TextureFormatFlags::None, 8, uint2(2, 2) }, // BC1_TYPELESS
	{ TextureFormatFlags::None, 8, uint2(2, 2) }, // BC1_UNORM
	{ TextureFormatFlags::None, 8, uint2(2, 2) }, // BC1_UNORM_SRGB
	
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC2_TYPELESS
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC2_UNORM
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC2_UNORM_SRGB
	
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC3_TYPELESS
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC3_UNORM
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC3_UNORM_SRGB
	
	{ TextureFormatFlags::None, 8, uint2(2, 2) }, // BC4_TYPELESS
	{ TextureFormatFlags::None, 8, uint2(2, 2) }, // BC4_UNORM
	{ TextureFormatFlags::None, 8, uint2(2, 2) }, // BC4_SNORM
	
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC5_TYPELESS
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC5_UNORM
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC5_SNORM
	
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC6H_TYPELESS
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC6H_UFLOAT
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC6H_SFLOAT
	
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC7_TYPELESS
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC7_UNORM
	{ TextureFormatFlags::None, 16, uint2(2, 2) }, // BC7_UNORM_SRGB
	
	{ TextureFormatFlags::Depth, 2, uint2(0, 0) }, // D16_UNORM
	{ TextureFormatFlags::Depth, 4, uint2(0, 0) }, // D32_FLOAT
	{ TextureFormatFlags::DepthStencil, 8, uint2(0, 0) }, // D32_FLOAT_S8
	
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // R32_FLOAT_X8_TYPELESS
	{ TextureFormatFlags::None, 8, uint2(0, 0) }, // X32_TYPELESS_G8_UINT
};

ArrayView<TextureFormatInfo> texture_format_info_map = { texture_format_infos, (u64)TextureFormat::Count };
