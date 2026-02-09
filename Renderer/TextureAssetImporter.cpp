#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMath.h"
#include "TextureAsset.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_DXT_IMPLEMENTATION
#define STBI_NO_STDIO

#include <SDK/stb/stb_image.h>
#include <SDK/stb/stb_image_resize2.h>
#include <SDK/stb/stb_dxt.h>

compile_const u32 stb_image_texel_size_bytes = sizeof(u32);

template<TextureFormat format>
static void TextureEncodeBCx(u8* mip_data, u8* mip_data_blocks, uint2 mip_size_blocks, u32 mip_row_pitch, u32 mip_row_pitch_blocks) {
	ProfilerScope("TextureEncodeBCx");
	
	compile_const u32 bcx_block_size_texels = 4;
	compile_const u32 bcx_block_size_bytes  = format == TextureFormat::BC5_UNORM ? 16 : 8;
	
	alignas(64) u8 src_block_data[64];
	for (u32 y = 0; y < mip_size_blocks.y; y += 1) {
		auto* src = mip_data + mip_row_pitch * y * bcx_block_size_texels;
		auto* dst = mip_data_blocks + mip_row_pitch_blocks * y;
		
		for (u32 x = 0; x < mip_size_blocks.x; x += 1, src += bcx_block_size_texels * stb_image_texel_size_bytes, dst += bcx_block_size_bytes) {
			memcpy(src_block_data + 0,  src + mip_row_pitch * 0, 16);
			memcpy(src_block_data + 16, src + mip_row_pitch * 1, 16);
			memcpy(src_block_data + 32, src + mip_row_pitch * 2, 16);
			memcpy(src_block_data + 48, src + mip_row_pitch * 3, 16);
			
			if constexpr (format == TextureFormat::BC1_UNORM) {
				stb_compress_dxt_block(dst, src_block_data, 0, STB_DXT_HIGHQUAL);
			} else if constexpr (format == TextureFormat::BC4_UNORM) {
				stb_compress_bc4_block(dst, src_block_data, 4);
			} else if constexpr (format == TextureFormat::BC5_UNORM) {
				stb_compress_bc5_block(dst, src_block_data, 4);
			}
		}
	}
}

// Base address and row pitch must be aligned to at least 64 bytes. See EncodeHemiOctahedralMap for reference.
static void TextureEncodeHemiOctahedralMap(u8* mip_data, uint2 mip_size, u32 mip_row_pitch) {
	ProfilerScope("TextureEncodeHemiOctahedralMap");
	
	//
	// Encoding time of a 4k texture with a full MIP chain:
	// - AVX512: 5.49 ms
	// - AVX2:   9.79 ms
	// - Scalar: 72.6 ms
	//
	// TODO: Fallback to AVX2 when AVX512 is not available.
	//
#define TEXTURE_ENCODE_NORMAL_MAP_SIMD_WIDTH 512
#if (TEXTURE_ENCODE_NORMAL_MAP_SIMD_WIDTH == 512)
	auto u8_to_float = _mm512_set1_ps(2.f / 255.f);
	auto float_to_u8 = _mm512_set1_ps(0.5f * 255.f);
	auto mask_low_u8 = _mm512_set1_epi32(0xFF);
	auto mask_abs    = _mm512_castsi512_ps(_mm512_set1_epi32(0x7FFFFFFF));
	auto one         = _mm512_set1_ps(1.f);
	
	for (u32 y = 0; y < mip_size.y; y += 1) {
		auto* src = mip_data + mip_row_pitch * y;
		
		for (u32 x = 0; x < mip_size.x; x += 16, src += 64) {
			auto row = _mm512_load_epi32((__m512i*)src);
			
			// Decode [-1, +1] from 8 bit unorm and normalize:
			auto xs = _mm512_fmsub_ps(_mm512_cvtepi32_ps(_mm512_and_epi32(mask_low_u8, row)), u8_to_float, one);
			auto ys = _mm512_fmsub_ps(_mm512_cvtepi32_ps(_mm512_and_epi32(mask_low_u8, _mm512_srli_epi32(row, 8u))), u8_to_float, one);
			auto zs = _mm512_fmsub_ps(_mm512_cvtepi32_ps(_mm512_and_epi32(mask_low_u8, _mm512_srli_epi32(row, 16u))), u8_to_float, one);
			
			auto rcp_length = _mm512_div_ps(one, _mm512_fmadd_ps(xs, xs, _mm512_fmadd_ps(ys, ys,  _mm512_mul_ps(zs, zs)))); // ~30% of the time is spent on normalization.
			xs = _mm512_mul_ps(xs, rcp_length);
			ys = _mm512_mul_ps(ys, rcp_length);
			zs = _mm512_mul_ps(zs, rcp_length);
			
			// EncodeHemiOctahedralMap:
			auto rcp_one_norm = _mm512_div_ps(one, _mm512_add_ps(_mm512_and_ps(xs, mask_abs), _mm512_add_ps(_mm512_and_ps(ys, mask_abs), _mm512_and_ps(zs, mask_abs))));
			auto tx = _mm512_mul_ps(xs, rcp_one_norm);
			auto ty = _mm512_mul_ps(ys, rcp_one_norm);
			auto encoded_x = _mm512_cvtps_epi32(_mm512_fmadd_ps(_mm512_add_ps(tx, ty), float_to_u8, float_to_u8));
			auto encoded_y = _mm512_cvtps_epi32(_mm512_fmadd_ps(_mm512_sub_ps(tx, ty), float_to_u8, float_to_u8));
			auto encoded   = _mm512_or_epi32(encoded_x, _mm512_slli_epi32(encoded_y, 8u));
			
			_mm512_store_epi32((__m512i*)src, encoded);
		}
	}
#elif (TEXTURE_ENCODE_NORMAL_MAP_SIMD_WIDTH == 256)
	auto u8_to_float = _mm256_set1_ps(2.f / 255.f);
	auto float_to_u8 = _mm256_set1_ps(0.5f * 255.f);
	auto mask_low_u8 = _mm256_set1_epi32(0xFF);
	auto mask_abs    = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
	auto one         = _mm256_set1_ps(1.f);
	
	for (u32 y = 0; y < mip_size.y; y += 1) {
		auto* src = mip_data + mip_row_pitch * y;
		
		for (u32 x = 0; x < mip_size.x; x += 8, src += 32) {
			auto row = _mm256_load_si256((__m256i*)src);
			
			// Decode [-1, +1] from 8 bit unorm and normalize:
			auto xs = _mm256_fmsub_ps(_mm256_cvtepi32_ps(_mm256_and_epi32(mask_low_u8, row)), u8_to_float, one);
			auto ys = _mm256_fmsub_ps(_mm256_cvtepi32_ps(_mm256_and_epi32(mask_low_u8, _mm256_srli_epi32(row, 8u))), u8_to_float, one);
			auto zs = _mm256_fmsub_ps(_mm256_cvtepi32_ps(_mm256_and_epi32(mask_low_u8, _mm256_srli_epi32(row, 16u))), u8_to_float, one);
			
			auto rcp_length = _mm256_div_ps(one, _mm256_fmadd_ps(xs, xs, _mm256_fmadd_ps(ys, ys,  _mm256_mul_ps(zs, zs))));
			xs = _mm256_mul_ps(xs, rcp_length);
			ys = _mm256_mul_ps(ys, rcp_length);
			zs = _mm256_mul_ps(zs, rcp_length);
			
			// EncodeHemiOctahedralMap:
			auto rcp_one_norm = _mm256_div_ps(one, _mm256_add_ps(_mm256_and_ps(xs, mask_abs), _mm256_add_ps(_mm256_and_ps(ys, mask_abs), _mm256_and_ps(zs, mask_abs))));
			auto tx = _mm256_mul_ps(xs, rcp_one_norm);
			auto ty = _mm256_mul_ps(ys, rcp_one_norm);
			auto encoded_x = _mm256_cvtps_epi32(_mm256_fmadd_ps(_mm256_add_ps(tx, ty), float_to_u8, float_to_u8));
			auto encoded_y = _mm256_cvtps_epi32(_mm256_fmadd_ps(_mm256_sub_ps(tx, ty), float_to_u8, float_to_u8));
			auto encoded   = _mm256_or_si256(encoded_x, _mm256_slli_epi32(encoded_y, 8u));
			
			_mm256_store_si256((__m256i*)src, encoded);
		}
	}
#elif (TEXTURE_ENCODE_NORMAL_MAP_SIMD_WIDTH == 1)
	compile_const float u8_to_float = (2.f / 255.f);
	compile_const float float_to_u8 = (0.5f * 255.f);
	compile_const u32   mask_low_u8 = 0xFF;
	
	for (u32 y = 0; y < mip_size.y; y += 1) {
		auto* src = mip_data + mip_row_pitch * y;
		
		for (u32 x = 0; x < mip_size.x; x += 1, src += 4) {
			auto row = *(u32*)src;
			
			// Decode [-1, +1] from 8 bit unorm and normalize:
			auto xs = (float)((mask_low_u8 & row)) * u8_to_float - 1.f;
			auto ys = (float)((mask_low_u8 & (row >> 8u))) * u8_to_float - 1.f;
			auto zs = (float)((mask_low_u8 & (row >> 16u))) * u8_to_float - 1.f;
			
			auto rcp_length = (1.f / (xs * xs + ys * ys + zs * zs));
			xs = xs * rcp_length;
			ys = ys * rcp_length;
			zs = zs * rcp_length;
			
			// EncodeHemiOctahedralMap:
			auto rcp_one_norm = 1.f / (fabsf(xs) + fabsf(ys) + fabsf(zs));
			auto tx = xs * rcp_one_norm;
			auto ty = ys * rcp_one_norm;
			auto encoded_x = (u32)((tx + ty) * float_to_u8 + float_to_u8);
			auto encoded_y = (u32)((tx - ty) * float_to_u8 + float_to_u8);
			auto encoded   = (encoded_x | (encoded_y << 8u));
			
			*(u32*)src = encoded;
		}
	}
#endif // (TEXTURE_ENCODE_NORMAL_MAP_SIMD_WIDTH == 1)
}


TextureImportResult ImportTextureFile(StackAllocator* alloc, const TextureSourceData& source_data, u64 runtime_data_guid) {
	ProfilerScope("ImportTextureFile");
	TempAllocationScope(alloc);
	
	auto file_data = SystemReadFileToString(alloc, source_data.filepath);
	if (file_data.data == nullptr) return {};
	
	s32x2 stb_image_size;
	s32 stb_image_channel_count = 0;
	auto* stb_image_result = stbi_load_from_memory((u8*)file_data.data, (s32)file_data.count, &stb_image_size.x, &stb_image_size.y, &stb_image_channel_count, 4);
	if (stb_image_result == nullptr) return {};
	
	defer{ stbi_image_free(stb_image_result); };
	
	
	u32 max_image_size = (u32)Math::Max(Math::Max(stb_image_size.x, stb_image_size.y), 1);
	u32 mip_count = FirstBitHigh32(max_image_size) + 1;
	
	compile_const u32 max_mip_count = 16;
	if (mip_count > max_mip_count) return {};
	
	
	auto output_format = TextureFormat::BC1_UNORM_SRGB;
	switch (source_data.target_encoding) {
	case TextureAssetTargetEncoding::BC1_UNORM_SRGB: output_format = TextureFormat::BC1_UNORM_SRGB; break;
	case TextureAssetTargetEncoding::BC1_UNORM:      output_format = TextureFormat::BC1_UNORM; break;
	case TextureAssetTargetEncoding::BC4_UNORM:      output_format = TextureFormat::BC4_UNORM; break;
	case TextureAssetTargetEncoding::BC5_UNORM:      output_format = TextureFormat::BC5_UNORM; break;
	case TextureAssetTargetEncoding::BC5_NORMAL_MAP: output_format = TextureFormat::BC5_UNORM; break;
	}
	auto format = texture_format_info_map[(u32)output_format];
	
	
	u32 stb_image_row_pitch = stb_image_size.x * stb_image_texel_size_bytes;
	u32 mip_0_row_pitch = AlignUp(stb_image_row_pitch, texture_row_pitch_alignment);
	auto* mip_0_data = (u8*)alloc->Allocate(mip_0_row_pitch * AlignUp(stb_image_size.y, 1u << format.block_size_log2.y), texture_row_pitch_alignment);
	
	{
		ProfilerScope("PadMip0");
		
		for (s32 y = 0; y < stb_image_size.y; y += 1) {
			memcpy(mip_0_data + (y + 0) * mip_0_row_pitch, stb_image_result + (y + 0) * stb_image_row_pitch, stb_image_row_pitch);
		}
	}
	
	
	FixedCapacityArray<u8*, max_mip_count> mips;
	ArrayAppend(mips, mip_0_data);
	
	u8* last_mip_data      = mip_0_data;
	u32 last_mip_row_pitch = mip_0_row_pitch;
	uint2 last_mip_size    = uint2(stb_image_size);
	for (u32 mip_index = 1; mip_index < mip_count; mip_index += 1) {
		ProfilerScope("stbir_resize_uint8_xxx");
		
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		u32 mip_row_pitch = AlignUp(mip_size.x * stb_image_texel_size_bytes, texture_row_pitch_alignment);
		
		auto* mip_data = (u8*)alloc->Allocate(mip_row_pitch * AlignUp(mip_size.y, 1u << format.block_size_log2.y), texture_row_pitch_alignment);
		
		if (output_format == TextureFormat::BC1_UNORM_SRGB) {
			stbir_resize_uint8_srgb(
				last_mip_data, last_mip_size.x, last_mip_size.y, last_mip_row_pitch,
				mip_data,      mip_size.x,      mip_size.y,      mip_row_pitch,
				STBIR_RGBA
			);
		} else {
			stbir_resize_uint8_linear(
				last_mip_data, last_mip_size.x, last_mip_size.y, last_mip_row_pitch,
				mip_data,      mip_size.x,      mip_size.y,      mip_row_pitch,
				STBIR_RGBA
			);
		}
		
		last_mip_data      = mip_data;
		last_mip_row_pitch = mip_row_pitch;
		last_mip_size      = mip_size;
		
		ArrayAppend(mips, mip_data);
	}
	
	
	// Pad mips to block size.
	for (u32 mip_index = 0; mip_index < mips.count; mip_index += 1) {
		ProfilerScope("PadMipToBlockSize");
		
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		auto mip_size_blocks = (mip_size + (uint2(1u) << format.block_size_log2) - 1) >> format.block_size_log2;
		
		u32  mip_row_pitch = AlignUp(mip_size.x * stb_image_texel_size_bytes, texture_row_pitch_alignment);
		auto mip_full_size = mip_size_blocks << format.block_size_log2;
		
		if (mip_size.x < mip_full_size.x) {
			u8* mip_data = mips[mip_index] + (mip_size.x - 1) * stb_image_texel_size_bytes;
			u32 x_count = mip_full_size.x - mip_size.x;
			
			for (u32 y = 0; y < mip_size.y; y += 1) {
				for (u32 x_offset = 1; x_offset <= x_count; x_offset += 1) {
					memcpy(mip_data + x_offset * stb_image_texel_size_bytes, mip_data, stb_image_texel_size_bytes);
				}
				mip_data += mip_row_pitch;
			}
		}
		
		if (mip_size.y < mip_full_size.y) {
			u8* mip_data = mips[mip_index] + (mip_size.y - 1) * mip_row_pitch;
			u32 y_count = mip_full_size.y - mip_size.y;
			
			for (u32 y_offset = 1; y_offset <= y_count; y_offset += 1) {
				memcpy(mip_data + y_offset * mip_row_pitch, mip_data, mip_row_pitch);
			}
		}
	}
	
	
	if (source_data.target_encoding == TextureAssetTargetEncoding::BC5_NORMAL_MAP) {
		ProfilerScope("TextureEncodeNormalMapMips");
		
		for (u32 mip_index = 0; mip_index < mips.count; mip_index += 1) {
			auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
			u32 mip_row_pitch = AlignUp(mip_size.x * stb_image_texel_size_bytes, texture_row_pitch_alignment);
			
			TextureEncodeHemiOctahedralMap(mips[mip_index], mip_size, mip_row_pitch);
		}
	}
	
	
	FixedCapacityArray<u8*, max_mip_count> block_compressed_mips;
	for (u32 mip_index = 0; mip_index < mips.count; mip_index += 1) {
		ProfilerScope("CompressMipBlocks");
		
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		auto mip_size_blocks = (mip_size + (uint2(1u) << format.block_size_log2) - 1) >> format.block_size_log2;
		
		u32 mip_row_pitch = AlignUp(mip_size.x * stb_image_texel_size_bytes, texture_row_pitch_alignment);
		u32 mip_row_pitch_blocks = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment);
		
		u8* mip_data = mips[mip_index];
		u8* mip_data_blocks = (u8*)alloc->Allocate(mip_row_pitch_blocks * mip_size_blocks.y, texture_row_pitch_alignment);
		
		if (output_format == TextureFormat::BC1_UNORM_SRGB || output_format == TextureFormat::BC1_UNORM) {
			TextureEncodeBCx<TextureFormat::BC1_UNORM>(mip_data, mip_data_blocks, mip_size_blocks, mip_row_pitch, mip_row_pitch_blocks);
		} else if (output_format == TextureFormat::BC4_UNORM) {
			TextureEncodeBCx<TextureFormat::BC4_UNORM>(mip_data, mip_data_blocks, mip_size_blocks, mip_row_pitch, mip_row_pitch_blocks);
		} else if (output_format == TextureFormat::BC5_UNORM) {
			TextureEncodeBCx<TextureFormat::BC5_UNORM>(mip_data, mip_data_blocks, mip_size_blocks, mip_row_pitch, mip_row_pitch_blocks);
		}
		
		ArrayAppend(block_compressed_mips, mip_data_blocks);
	}
	
	
	auto runtime_filepath = StringFormat(alloc, "./Assets/Runtime/%x..trd"_sl, runtime_data_guid);
	
	auto runtime_file = SystemOpenFile(alloc, runtime_filepath, OpenFileFlags::Write);
	if (runtime_file.handle == nullptr) return {};
	bool write_file_success = true;
	
	u64 write_offset = 0;
	for (u32 mip_index = 0; mip_index < mip_count; mip_index += 1) {
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		auto mip_size_blocks = (mip_size + (uint2(1u) << format.block_size_log2) - 1) >> format.block_size_log2;
		
		u32 mip_size_bytes = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
		write_file_success &= SystemWriteFile(runtime_file, block_compressed_mips[mip_index], mip_size_bytes, write_offset);
		write_offset += mip_size_bytes;
	}
	write_file_success &= SystemCloseFile(runtime_file);
	
	
	TextureRuntimeDataLayout layout;
	layout.file_guid = runtime_data_guid;
	layout.version   = TextureRuntimeDataLayout::current_version;
	layout.size      = TextureSize(output_format, uint2(stb_image_size), 1, mip_count);
	
	return { layout, write_file_success };
}
