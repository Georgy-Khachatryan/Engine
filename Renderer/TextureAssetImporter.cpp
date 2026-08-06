#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicThreads.h"
#include "TextureAsset.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_DXT_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS 256
#define STBIR_MALLOC(size, user_data) (((StackAllocator*)(user_data))->Allocate(size, texture_row_pitch_alignment))
#define STBIR_FREE(size, user_data) ((void)(size)),((void)(user_data))
#define STBI_MALLOC(size, user_data) (((StackAllocator*)(user_data))->Allocate(size, texture_row_pitch_alignment))
#define STBI_REALLOC(data, old_size, new_size, user_data) (((StackAllocator*)(user_data))->Reallocate(data, old_size, new_size, texture_row_pitch_alignment))
#define STBI_FREE(data, user_data) ((void)(data)), ((void)(user_data))

#include <SDK/stb/stb_image.h>
#include <SDK/stb/stb_image_resize2.h>
#include <SDK/stb/stb_dxt.h>
#include <SDK/D3D12/include/dxgiformat.h>

static constexpr u32 MakeFourCC(const char* data) { return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24); }

compile_const u32 stb_image_texel_size_bytes = sizeof(u32);

template<TextureFormat format>
static void TextureEncodeBCx(ThreadPool* thread_pool, u8* mip_data, u8* mip_data_blocks, uint2 mip_size_blocks, u32 mip_row_pitch, u32 mip_row_pitch_blocks) {
	ProfilerScope("TextureEncodeBCx");
	
	compile_const u32 bcx_block_size_texels = 4;
	compile_const u32 bcx_block_size_bytes  = format == TextureFormat::BC5_UNORM ? 16 : 8;
	compile_const u32 blocks_per_thread     = 1024;
	
	u32 thread_count    = DivideAndRoundUp(mip_size_blocks.x * mip_size_blocks.y, blocks_per_thread);
	u32 rows_per_thread = DivideAndRoundUp(mip_size_blocks.y, thread_count);
	
	ParallelFor(thread_pool, 0, mip_size_blocks.y, rows_per_thread, [&](u64 y, u32 thread_index) {
		auto* src = mip_data + mip_row_pitch * y * bcx_block_size_texels;
		auto* dst = mip_data_blocks + mip_row_pitch_blocks * y;
		
		alignas(64) u8 src_block_data[64];
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
	});
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

static bool WriteTextureToRuntimeFile(StackAllocator* alloc, ArrayView<u8*> subresources, TextureSize size, u64 runtime_data_guid) {
	auto runtime_filepath = StringFormat(alloc, "./Assets/Runtime/%x..bin"_sl, runtime_data_guid);
	
	auto runtime_file = SystemOpenFile(alloc, runtime_filepath, OpenFileFlags::Write);
	if (runtime_file.handle == nullptr) return false;
	
	auto format = texture_format_info_map[(u32)size.format];
	bool write_file_success = true;
	
	u64 write_offset = 0;
	for (u32 array_index = 0; array_index < size.ArraySliceCount(); array_index += 1) {
		for (u32 mip_index = 0; mip_index < size.mips; mip_index += 1) {
			auto mip_size_texels = Math::Max(uint3(size.x, size.y, size.DepthSliceCount()) >> mip_index, 1u);
			auto mip_size_blocks = DivideAndRoundUpLog2(mip_size_texels, uint3(format.block_size_log2, 0u));
			auto mip_size_bytes  = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y * mip_size_blocks.z;
			
			write_file_success &= SystemWriteFile(runtime_file, subresources[array_index * size.mips + mip_index], mip_size_bytes, write_offset);
			write_offset += mip_size_bytes;
		}
	}
	write_file_success &= SystemCloseFile(runtime_file);
	
	return write_file_success;
}


static TextureImportResult ImportTextureFileSTB(StackAllocator* alloc, ThreadPool* thread_pool, String file_data, const TextureSourceData& source_data, u64 runtime_data_guid);
static TextureImportResult ImportTextureFileDDS(StackAllocator* alloc, ThreadPool* thread_pool, String file_data, const TextureSourceData& source_data, u64 runtime_data_guid);

TextureImportResult ImportTextureFile(StackAllocator* alloc, ThreadPool* thread_pool, const TextureSourceData& source_data, u64 runtime_data_guid) {
	ProfilerScope("ImportTextureFile");
	TempAllocationScope(alloc);
	
	auto file_data = SystemReadFileToString(alloc, source_data.filepath);
	if (file_data.data == nullptr || file_data.count < 4) return {};
	
	u32 magic = 0;
	memcpy(&magic, file_data.data, 4);
	
	TextureImportResult result;
	if (magic == MakeFourCC("DDS ")) {
		result = ImportTextureFileDDS(alloc, thread_pool, file_data, source_data, runtime_data_guid);
	} else {
		result = ImportTextureFileSTB(alloc, thread_pool, file_data, source_data, runtime_data_guid);
	}
	
	return result;
}

static TextureImportResult ImportTextureFileSTB(StackAllocator* alloc, ThreadPool* thread_pool, String file_data, const TextureSourceData& source_data, u64 runtime_data_guid) {
	ProfilerScope("ImportTextureFileSTB");
	
	s32x2 stb_image_size;
	s32 stb_image_channel_count = 0;
	auto* stb_image_result = stbi_load_from_memory((u8*)file_data.data, (s32)file_data.count, &stb_image_size.x, &stb_image_size.y, &stb_image_channel_count, 4, alloc);
	if (stb_image_result == nullptr) return {};
	
	
	u32 max_image_size = (u32)Math::Max(Math::Max(stb_image_size.x, stb_image_size.y), 1);
	u32 mip_count = FirstBitHigh32(max_image_size) + 1;
	
	if (mip_count > texture_max_mip_level_count) return {};
	
	
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
			memcpy(mip_0_data + y * mip_0_row_pitch, stb_image_result + y * stb_image_row_pitch, stb_image_row_pitch);
		}
	}
	
	
	FixedCapacityArray<u8*, texture_max_mip_level_count> mips;
	ArrayAppend(mips, mip_0_data);
	
	u8* last_mip_data      = mip_0_data;
	u32 last_mip_row_pitch = mip_0_row_pitch;
	uint2 last_mip_size    = uint2(stb_image_size);
	for (u32 mip_index = 1; mip_index < mip_count; mip_index += 1) {
		ProfilerScope("stbir_resize_uint8_xxx");
		
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		u32 mip_row_pitch = AlignUp(mip_size.x * stb_image_texel_size_bytes, texture_row_pitch_alignment);
		
		auto* mip_data = (u8*)alloc->Allocate(mip_row_pitch * AlignUp(mip_size.y, 1u << format.block_size_log2.y), texture_row_pitch_alignment);
		
		TempAllocationScope(alloc);
		STBIR_RESIZE resize_state = {};
		
		stbir_resize_init(
			&resize_state,
			last_mip_data, last_mip_size.x, last_mip_size.y, last_mip_row_pitch,
			mip_data,      mip_size.x,      mip_size.y,      mip_row_pitch,
			STBIR_RGBA,
			output_format == TextureFormat::BC1_UNORM_SRGB ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8
		);
		
		stbir_set_user_data(&resize_state, alloc);
		
		s32 split_count = stbir_build_samplers_with_splits(&resize_state, 8);
		
		ParallelFor(thread_pool, 0, split_count, 1, [&](u64 split_index, u32 thread_index) {
			stbir_resize_extended_split(&resize_state, (s32)split_index, 1);
		});
		
		// Technically not needed because of the TempAllocationScope.
		stbir_free_samplers(&resize_state);
		
		last_mip_data      = mip_data;
		last_mip_row_pitch = mip_row_pitch;
		last_mip_size      = mip_size;
		
		ArrayAppend(mips, mip_data);
	}
	
	
	// Pad mips to block size.
	for (u32 mip_index = 0; mip_index < mips.count; mip_index += 1) {
		ProfilerScope("PadMipToBlockSize");
		
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		auto mip_size_blocks = DivideAndRoundUpLog2(mip_size, format.block_size_log2);
		
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
	
	
	FixedCapacityArray<u8*, texture_max_mip_level_count> block_compressed_mips;
	for (u32 mip_index = 0; mip_index < mips.count; mip_index += 1) {
		ProfilerScope("CompressMipBlocks");
		
		auto mip_size = Math::Max(uint2(stb_image_size) >> mip_index, uint2(1u));
		auto mip_size_blocks = DivideAndRoundUpLog2(mip_size, format.block_size_log2);
		
		u32 mip_row_pitch = AlignUp(mip_size.x * stb_image_texel_size_bytes, texture_row_pitch_alignment);
		u32 mip_row_pitch_blocks = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment);
		
		u8* mip_data = mips[mip_index];
		u8* mip_data_blocks = (u8*)alloc->Allocate(mip_row_pitch_blocks * mip_size_blocks.y, texture_row_pitch_alignment);
		
		if (output_format == TextureFormat::BC1_UNORM_SRGB || output_format == TextureFormat::BC1_UNORM) {
			TextureEncodeBCx<TextureFormat::BC1_UNORM>(thread_pool, mip_data, mip_data_blocks, mip_size_blocks, mip_row_pitch, mip_row_pitch_blocks);
		} else if (output_format == TextureFormat::BC4_UNORM) {
			TextureEncodeBCx<TextureFormat::BC4_UNORM>(thread_pool, mip_data, mip_data_blocks, mip_size_blocks, mip_row_pitch, mip_row_pitch_blocks);
		} else if (output_format == TextureFormat::BC5_UNORM) {
			TextureEncodeBCx<TextureFormat::BC5_UNORM>(thread_pool, mip_data, mip_data_blocks, mip_size_blocks, mip_row_pitch, mip_row_pitch_blocks);
		}
		
		ArrayAppend(block_compressed_mips, mip_data_blocks);
	}
	
	TextureRuntimeDataLayout layout;
	layout.file_guid = runtime_data_guid;
	layout.version   = TextureRuntimeDataLayout::current_version;
	layout.size      = TextureSize(output_format, uint2(stb_image_size), 1, mip_count);
	
	bool write_file_success = WriteTextureToRuntimeFile(alloc, block_compressed_mips, layout.size, runtime_data_guid);
	
	return { layout, write_file_success };
}

static TextureImportResult ImportTextureFileDDS(StackAllocator* alloc, ThreadPool* thread_pool, String file_data, const TextureSourceData& source_data, u64 runtime_data_guid) {
	ProfilerScope("ImportTextureFileDDS");
	
	enum DDS_PIXELFORMAT_FLAGS {
		DDPF_ALPHAPIXELS = 0x1,
		DDPF_ALPHA       = 0x2,
		DDPF_FOURCC      = 0x4,
		DDPF_RGB         = 0x40,
		DDPF_YUV         = 0x20000,
	};
	
	struct DDS_PIXELFORMAT {
		u32 dwSize;
		u32 dwFlags;
		u32 dwFourCC;
		u32 dwRGBBitCount;
		u32 dwRBitMask;
		u32 dwGBitMask;
		u32 dwBBitMask;
		u32 dwABitMask;
	};
	
	enum DDS_HEADER_FLAGS {
		DDSD_CAPS        = 0x1,
		DDSD_HEIGHT      = 0x2,
		DDSD_WIDTH       = 0x4,
		DDSD_PITCH       = 0x8,
		DDSD_PIXELFORMAT = 0x1000,
		DDSD_MIPMAPCOUNT = 0x20000,
		DDSD_LINEARSIZE  = 0x80000,
		DDSD_DEPTH       = 0x800000,
	};
	
	enum DDSCAPS2_FLAGS {
		DDSCAPS2_CUBEMAP = 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000,
		DDSCAPS2_VOLUME  = 0x200000,
	};
	
	struct DDS_HEADER {
		u32 dwSize;
		u32 dwFlags;
		u32 dwHeight;
		u32 dwWidth;
		u32 dwPitchOrLinearSize;
		u32 dwDepth;
		u32 dwMipMapCount;
		u32 dwReserved1[11];
		DDS_PIXELFORMAT ddspf;
		u32 dwCaps;
		u32 dwCaps2;
		u32 dwCaps3;
		u32 dwCaps4;
		u32 dwReserved2;
	};
	
	enum D3D10_RESOURCE_DIMENSION {
		D3D10_RESOURCE_DIMENSION_UNKNOWN   = 0,
		D3D10_RESOURCE_DIMENSION_BUFFER    = 1,
		D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
		D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
		D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4,
	};
	
	struct DDS_HEADER_DXT10 {
		DXGI_FORMAT dxgiFormat;
		D3D10_RESOURCE_DIMENSION resourceDimension;
		u32 miscFlag;
		u32 arraySize;
		u32 miscFlags2;
	};
	
	enum D3DFORMAT {
		D3DFMT_G16R16        = 34,
		D3DFMT_A16B16G16R16  = 36,
		D3DFMT_V16U16        = 64,
		D3DFMT_Q16W16V16U16  = 110,
		D3DFMT_R16F          = 111,
		D3DFMT_G16R16F       = 112,
		D3DFMT_A16B16G16R16F = 113,
		D3DFMT_R32F          = 114,
		D3DFMT_G32R32F       = 115,
		D3DFMT_A32B32G32R32F = 116,
	}; 
	
	
	if (file_data.count < sizeof(u32) + sizeof(DDS_HEADER)) return {};
	
	u64 cursor = 0;
	
	u32 dds_magic = 0;
	memcpy(&dds_magic, file_data.data + cursor, sizeof(u32));
	cursor += sizeof(u32);
	
	if (dds_magic != MakeFourCC("DDS ")) return {};
	
	DDS_HEADER dds_header = {};
	memcpy(&dds_header, file_data.data + cursor, sizeof(DDS_HEADER));
	cursor += sizeof(DDS_HEADER);
	
	bool has_dds_dx10_header = (dds_header.dwFlags & DDPF_FOURCC) != 0 && (dds_header.ddspf.dwFourCC == MakeFourCC("DX10"));
	
	DDS_HEADER_DXT10 dds_dx10_header = {};
	if (has_dds_dx10_header) {
		if (file_data.count - cursor < sizeof(DDS_HEADER_DXT10)) return {};
		
		memcpy(&dds_dx10_header, file_data.data + cursor, sizeof(DDS_HEADER_DXT10));
		cursor += sizeof(DDS_HEADER_DXT10);
	}
	
	auto dxgi_format = DXGI_FORMAT_UNKNOWN;
	if (has_dds_dx10_header) {
		dxgi_format = dds_dx10_header.dxgiFormat;
	} else if ((dds_header.dwFlags & DDPF_FOURCC) != 0) {
		switch (dds_header.ddspf.dwFourCC) {
		case MakeFourCC("DXT1"): dxgi_format = DXGI_FORMAT_BC1_UNORM; break;
		case MakeFourCC("DXT2"): dxgi_format = DXGI_FORMAT_BC2_UNORM; break;
		case MakeFourCC("DXT3"): dxgi_format = DXGI_FORMAT_BC2_UNORM; break;
		case MakeFourCC("DXT4"): dxgi_format = DXGI_FORMAT_BC3_UNORM; break;
		case MakeFourCC("DXT5"): dxgi_format = DXGI_FORMAT_BC3_UNORM; break;
		case MakeFourCC("BC4U"): dxgi_format = DXGI_FORMAT_BC4_UNORM; break;
		case MakeFourCC("BC4S"): dxgi_format = DXGI_FORMAT_BC4_SNORM; break;
		case MakeFourCC("BC5U"): dxgi_format = DXGI_FORMAT_BC5_UNORM; break;
		case MakeFourCC("BC5S"): dxgi_format = DXGI_FORMAT_BC5_SNORM; break;
		
		case D3DFMT_G16R16:        dxgi_format = DXGI_FORMAT_R16G16_UNORM;       break;
		case D3DFMT_A16B16G16R16:  dxgi_format = DXGI_FORMAT_R16G16B16A16_UNORM; break;
		case D3DFMT_V16U16:        dxgi_format = DXGI_FORMAT_R16G16_SNORM;       break;
		case D3DFMT_Q16W16V16U16:  dxgi_format = DXGI_FORMAT_R16G16B16A16_SNORM; break;
		case D3DFMT_R16F:          dxgi_format = DXGI_FORMAT_R16_FLOAT;          break;
		case D3DFMT_G16R16F:       dxgi_format = DXGI_FORMAT_R16G16_FLOAT;       break;
		case D3DFMT_A16B16G16R16F: dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
		case D3DFMT_R32F:          dxgi_format = DXGI_FORMAT_R32_FLOAT;          break;
		case D3DFMT_G32R32F:       dxgi_format = DXGI_FORMAT_R32G32_FLOAT;       break;
		case D3DFMT_A32B32G32R32F: dxgi_format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
		}
	} else {
		u32 channel_mask = 0;
		channel_mask |= dds_header.ddspf.dwRBitMask == (0xFF << 0)  ? 0x1 : 0;
		channel_mask |= dds_header.ddspf.dwGBitMask == (0xFF << 8)  ? 0x2 : 0;
		channel_mask |= dds_header.ddspf.dwBBitMask == (0xFF << 16) ? 0x4 : 0;
		channel_mask |= dds_header.ddspf.dwABitMask == (0xFF << 24) ? 0x8 : 0;
		
		if ((dds_header.ddspf.dwRGBBitCount == 32) && (channel_mask == 0xF)) {
			dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
		} else if ((dds_header.ddspf.dwRGBBitCount == 16) && (channel_mask == 0x3)) {
			dxgi_format = DXGI_FORMAT_R8G8_UNORM;
		} else if ((dds_header.ddspf.dwRGBBitCount == 8) && (channel_mask == 0x1)) {
			dxgi_format = DXGI_FORMAT_R8_UNORM;
		}
	}
	
	auto output_format = TextureFormat::None;
	for (u32 i = 0; i < (u32)TextureFormat::Count; i += 1) {
		if (dxgi_texture_format_map[i] == dxgi_format) {
			output_format = (TextureFormat)i;
			break;
		}
	}
	
	if (output_format == TextureFormat::None) return {};
	
	
	uint3 size = Math::Max(uint3(dds_header.dwWidth, dds_header.dwHeight, dds_header.dwDepth), 1u);
	u32 array_size = has_dds_dx10_header ? Math::Max(dds_dx10_header.arraySize, 1u) : 1;
	u32 mip_count  = Math::Max(dds_header.dwMipMapCount, 1u);
	
	
	bool is_volume  = (dds_header.dwCaps2 & DDSCAPS2_VOLUME)  != 0;
	bool is_cubemap = (dds_header.dwCaps2 & DDSCAPS2_CUBEMAP) != 0;
	
	if (is_cubemap) return {}; // Cubemaps are not supported.
	
	if (is_volume == false && size.z > 1) return {}; // 2D texture, but has depth slices?
	
	if (array_size > 1 && size.z > 1) return {}; // 2D texture array?
	
	u32 max_texture_size = (u32)Math::Max(Math::Max(size.x, size.y), size.z);
	u32 max_mip_count = FirstBitHigh32(max_texture_size) + 1;
	
	if (mip_count > max_mip_count) return {};
	
	auto texture_type = size.z > 1 ? TextureSizeType::Texture3D : TextureSizeType::Texture2D;
	if (texture_type == TextureSizeType::Texture2D) {
		if (size.x > 16384 || size.y > 16384 || array_size > 2048) return {};
	} else /*if (texture_type == TextureSizeType::Texture3D)*/ {
		if (size.x > 2048 || size.y > 2048 || size.z > 2048) return {};
	}
	
	
	Array<u8*> block_compressed_mips;
	ArrayResizeMemset(block_compressed_mips, alloc, array_size * mip_count);
	
	auto format = texture_format_info_map[(u32)output_format];
	for (u32 array_index = 0; array_index < array_size; array_index += 1) {
		for (u32 mip_index = 0; mip_index < mip_count; mip_index += 1) {
			auto mip_size_texels = Math::Max(size >> mip_index, 1u);
			auto mip_size_blocks = DivideAndRoundUpLog2(mip_size_texels, uint3(format.block_size_log2, 0u));
			
			u32 row_size  = mip_size_blocks.x * format.block_size_bytes;
			u32 row_pitch = AlignUp(row_size, texture_row_pitch_alignment);
			
			if (row_size * mip_size_blocks.y * mip_size_blocks.z > (file_data.count - cursor)) return {};
			
			
			u32 mip_size_bytes = row_pitch * mip_size_blocks.y * mip_size_blocks.z;
			
			u8* mip_data_blocks = nullptr;
			if (row_size == row_pitch) {
				mip_data_blocks = (u8*)(file_data.data + cursor);
				cursor += mip_size_bytes;
			} else {
				mip_data_blocks = (u8*)alloc->Allocate(mip_size_bytes, texture_row_pitch_alignment);
				
				for (u32 y = 0; y < mip_size_blocks.y * mip_size_blocks.z; y += 1) {
					memcpy(mip_data_blocks + y * row_pitch, file_data.data + cursor, row_size);
					memset(mip_data_blocks + y * row_pitch + row_size, 0, row_pitch - row_size);
					cursor += row_size;
				}
			}
			block_compressed_mips[array_index * mip_count + mip_index] = mip_data_blocks;
		}
	}
	
	TextureRuntimeDataLayout layout;
	layout.file_guid = runtime_data_guid;
	layout.version   = TextureRuntimeDataLayout::current_version;
	layout.size      = TextureSize(output_format, size.xy, Math::Max(size.z, array_size), mip_count, texture_type);
	
	bool write_file_success = WriteTextureToRuntimeFile(alloc, block_compressed_mips, layout.size, runtime_data_guid);
	
	return { layout, write_file_success };
}
