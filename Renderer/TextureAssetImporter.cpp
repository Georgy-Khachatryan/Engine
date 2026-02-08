#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMath.h"
#include "TextureAsset.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBI_NO_STDIO

#include <SDK/stb/stb_image.h>
#include <SDK/stb/stb_image_resize2.h>

TextureImportResult ImportTextureFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid) {
	ProfilerScope("ImportTextureFile");
	TempAllocationScope(alloc);
	
	auto file_data = SystemReadFileToString(alloc, filepath);
	if (file_data.data == nullptr) return {};
	
	s32x2 stb_image_size;
	s32 stb_image_channel_count = 0;
	auto* stb_image_result = stbi_load_from_memory((u8*)file_data.data, (s32)file_data.count, &stb_image_size.x, &stb_image_size.y, &stb_image_channel_count, 4);
	if (stb_image_result == nullptr) return {};
	
	bool free_stb_image_data = true;
	defer{ if (free_stb_image_data) stbi_image_free(stb_image_result); };
	
	
	u32 max_image_size = (u32)Math::Max(Math::Max(stb_image_size.x, stb_image_size.y), 1);
	u32 mip_count = FirstBitHigh32(max_image_size) + 1;
	
	compile_const u32 max_mip_count = 16;
	if (mip_count > max_mip_count) return {};
	
	u32 stb_image_row_pitch = stb_image_size.x * 4;
	if ((stb_image_row_pitch % texture_row_pitch_alignment) != 0) {
		u32 row_pitch = AlignUp(stb_image_row_pitch, texture_row_pitch_alignment);
		auto* padded_image = (u8*)alloc->Allocate(row_pitch * stb_image_size.y);
		
		for (s32 y = 0; y < stb_image_size.y; y += 1) {
			memcpy(padded_image + y * row_pitch, stb_image_result + y * stb_image_row_pitch, stb_image_row_pitch);
		}
		stbi_image_free(stb_image_result);
		
		stb_image_result    = padded_image;
		stb_image_row_pitch = row_pitch;
		free_stb_image_data = false;
	}
	
	
	FixedCapacityArray<u8*, 16> mips;
	ArrayAppend(mips, stb_image_result);
	
	u8* last_mip_data      = stb_image_result;
	u32 last_mip_row_pitch = stb_image_row_pitch;
	s32x2 last_mip_size    = stb_image_size;
	for (u32 mip_index = 1; mip_index < mip_count; mip_index += 1) {
		ProfilerScope("stbir_resize_uint8_srgb");
		
		s32x2 mip_size;
		mip_size.x = Math::Max(stb_image_size.x >> mip_index, 1);
		mip_size.y = Math::Max(stb_image_size.y >> mip_index, 1);
		
		u32 mip_row_pitch = AlignUp((u32)mip_size.x * 4, texture_row_pitch_alignment);
		
		auto* mip = stbir_resize_uint8_srgb(
			last_mip_data, last_mip_size.x, last_mip_size.y, last_mip_row_pitch,
			nullptr,       mip_size.x,      mip_size.y,      mip_row_pitch,
			STBIR_RGBA
		);
		last_mip_data      = mip;
		last_mip_row_pitch = mip_row_pitch;
		last_mip_size      = mip_size;
		
		ArrayAppend(mips, mip);
	}
	defer{ for (u32 mip_index = 1; mip_index < mip_count; mip_index += 1) STBIR_FREE(mips[mip_index], nullptr); };
	
	
	auto runtime_filepath = StringFormat(alloc, "./Assets/Runtime/%x..trd"_sl, runtime_data_guid);
	
	auto runtime_file = SystemOpenFile(alloc, runtime_filepath, OpenFileFlags::Write);
	if (runtime_file.handle == nullptr) return {};
	bool write_file_success = true;
	
	u64 write_offset = 0;
	for (u32 mip_index = 0; mip_index < mip_count; mip_index += 1) {
		u32 mip_size = AlignUp((u32)Math::Max(stb_image_size.x >> mip_index, 1) * 4, texture_row_pitch_alignment) * Math::Max(stb_image_size.y >> mip_index, 1);
		write_file_success &= SystemWriteFile(runtime_file, mips[mip_index], mip_size, write_offset);
		write_offset += mip_size;
	}
	
	TextureRuntimeDataLayout layout;
	layout.file_guid = runtime_data_guid;
	layout.version   = TextureRuntimeDataLayout::current_version;
	layout.size      = TextureSize(TextureFormat::R8G8B8A8_UNORM_SRGB, uint2(stb_image_size), 1, mip_count);
	
	write_file_success &= SystemCloseFile(runtime_file);
	
	return { layout, write_file_success };
}
