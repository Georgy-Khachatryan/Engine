#include "Basic/BasicBitArray.h"
#include "Basic/BasicMemory.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/RecordContext.h"
#include "RendererEntities.h"
#include "TextureAsset.h"
#include "TextureStreamingSystem.h"

struct TextureStreamingSystem {
	u64 heap_size = 0;
};

TextureStreamingSystem* CreateTextureStreamingSystem(StackAllocator* alloc, u64 heap_size) {
	auto* system = NewFromAlloc(alloc, TextureStreamingSystem);
	system->heap_size = heap_size;
	
	return system;
}

void UpdateTextureStreamingSystem(TextureStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* texture_streaming_feedback_queue) {
	auto element = texture_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return;
	
#if 0
	u32* texture_streaming_feedback_data = (u32*)element.data;
	for (u32 i = 0; i < persistent_srv_descriptor_count; i += 1) {
		u32 mip_level = texture_streaming_feedback_data[i];
		if (mip_level == u32_max) continue;
		
		SystemWriteToConsole(record_context->alloc, "Texture: %, MipLevel: %\n"_sl, i, *(float*)&mip_level);
	}
#endif
}

void UpdateTextureStreamingFiles(TextureStreamingSystem* system, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system) {
	auto* alloc = record_context->alloc;
	extern TextureImportResult ImportTextureFile(StackAllocator* alloc, ThreadPool* thread_pool, const TextureSourceData& source_data, u64 runtime_data_guid);
	
	u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
	auto* graphics_context = record_context->context;
	for (auto* entity_array : QueryEntities<TextureAssetType>(alloc, *asset_system)) {
		ProfilerScope("TextureAssetTypeGpuComponentUpdate");
		
		auto streams = ExtractComponentStreams<TextureAssetType>(entity_array);
		for (u64 i : BitArrayIt(entity_array->created_mask)) {
			auto& layout = streams.runtime_data_layout[i];
			auto& runtime_file = streams.runtime_file[i];
			auto& resource_allocation = streams.resource_allocation[i];
			auto& descriptor_allocation = streams.descriptor_allocation[i];
			
			if (completed_file_read_index < resource_allocation.file_read_wait_index) continue;
			
			if (layout.version != TextureRuntimeDataLayout::current_version) {
				if (layout.file_guid == 0) {
					layout.file_guid = GenerateRandomNumber64(asset_system->guid_random_seed);
				}
				
				if (runtime_file.file.handle != nullptr) {
					SystemCloseFile(runtime_file.file);
					runtime_file = {};
				}
				
				if (resource_allocation.resource.handle != nullptr) {
					ReleaseTextureResource(graphics_context, resource_allocation.resource, ResourceReleaseCondition::EndOfThisGpuFrame);
					resource_allocation = {};
				}
				
				auto result = ImportTextureFile(alloc, thread_pool, streams.source_data[i], layout.file_guid);
				if (result.success) {
					layout = result.layout;
				}
			}
			
			// TODO: Add support for unaligned async reads.
			runtime_file.file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..trd"_sl, layout.file_guid), OpenFileFlags::Read /*| OpenFileFlags::Async*/);
			
			u32 descriptor_index = descriptor_allocation.index == u32_max ? AllocatePersistentSrvDescriptor(graphics_context) : descriptor_allocation.index;
			descriptor_allocation.index = descriptor_index;
			
			auto texture_id = (VirtualResourceID)0;
			if (runtime_file.file.handle != nullptr) {
				auto resource = CreateTextureResource(graphics_context, layout.size);
				texture_id = record_context->resource_table->AddTransient(resource, layout.size);
				
				auto format = texture_format_info_map[(u32)layout.size.format];
				
				u64 offset = 0;
				u64 file_read_wait_index = 0;
				for (u32 mip_index = 0; mip_index < (u32)layout.size.mips; mip_index += 1) {
					auto mip_size = Math::Max(uint2(layout.size) >> mip_index, uint2(1u));
					auto mip_size_blocks = (mip_size + (uint2(1u) << format.block_size_log2) - 1) >> format.block_size_log2;
					
					AsyncTransferCommand command;
					command.src_type = AsyncTransferSrcType::File;
					command.dst_type = AsyncTransferDstType::Texture;
					command.src.file.handle      = runtime_file.file;
					command.src.file.offset      = offset;
					command.src.file.size        = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
					command.dst.texture.resource = resource;
					command.dst.texture.size     = TextureSize(layout.size.format, mip_size);
					command.dst.texture.offset   = 0;
					command.dst.texture.subresource_index = mip_index;
					file_read_wait_index = AppendAsyncTransferCommand(async_transfer_queue, command);
					
					offset += command.src.file.size;
				}
				
				resource_allocation.resource = resource;
				resource_allocation.file_read_wait_index = file_read_wait_index;
			}
			
			CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>(texture_id), descriptor_index);
		}
		
		for (u64 i : BitArrayIt(entity_array->removed_mask)) {
			u32 index = streams.descriptor_allocation[i].index;
			if (index != u32_max) DeallocatePersistentSrvDescriptor(graphics_context, index);
			
			auto resource = streams.resource_allocation[i].resource;
			if (resource.handle != nullptr) ReleaseTextureResource(graphics_context, resource, ResourceReleaseCondition::EndOfThisGpuFrame);
		}
	}
}
