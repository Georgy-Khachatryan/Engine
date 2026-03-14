#include "Basic/BasicBitArray.h"
#include "Basic/BasicMemory.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/RecordContext.h"
#include "RendererEntities.h"
#include "TextureAsset.h"
#include "TextureStreamingSystem.h"

struct TextureStreamingSystem {
	Array<EntityID> descriptor_index_to_texture_entity_id;
	Array<u8>       streamed_mip_level;
	
	u64 heap_size = 0;
};

TextureStreamingSystem* CreateTextureStreamingSystem(StackAllocator* alloc, u64 heap_size) {
	auto* system = NewFromAlloc(alloc, TextureStreamingSystem);
	system->heap_size = heap_size;
	
	ArrayResizeMemset(system->descriptor_index_to_texture_entity_id, alloc, persistent_srv_descriptor_count, 0xFF);
	ArrayResizeMemset(system->streamed_mip_level,                    alloc, persistent_srv_descriptor_count, 0xFF);
	
	return system;
}

void UpdateTextureStreamingSystem(TextureStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* texture_streaming_feedback_queue) {
	auto element = texture_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return;
	
	auto streams = ExtractComponentStreams<TextureAssetType>(QueryEntityTypeArray<TextureAssetType>(*asset_system));
	
	u32* texture_streaming_feedback_data = (u32*)element.data;
	for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
		u32 packed_feedback = texture_streaming_feedback_data[descriptor_index];
		if (packed_feedback == u32_max) continue;
		
		u32 target_mip_level = (u32)*(float*)&packed_feedback;
		auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
		
		auto& layout = streams.runtime_data_layout[entity_id.index];
		auto& runtime_file = streams.runtime_file[entity_id.index];
		auto& resource_allocation = streams.resource_allocation[entity_id.index];
		
		if (runtime_file.file.handle == nullptr) continue;
		
		
		u32 streamed_mip_level = system->streamed_mip_level[descriptor_index];
		if (target_mip_level >= streamed_mip_level) continue;
		
		// SystemWriteToConsole(record_context->alloc, "Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, target_mip_level);
		system->streamed_mip_level[descriptor_index] = target_mip_level;
		
		auto format = texture_format_info_map[(u32)layout.size.format];
		
		u64 offset = 0;
		for (u32 mip_index = 0; mip_index < Math::Min(streamed_mip_level, (u32)layout.size.mips); mip_index += 1) {
			auto mip_size = Math::Max(uint2(layout.size) >> mip_index, uint2(1u));
			auto mip_size_blocks = (mip_size + (uint2(1u) << format.block_size_log2) - 1) >> format.block_size_log2;
			
			AsyncTransferCommand command;
			command.src_type = AsyncTransferSrcType::File;
			command.dst_type = AsyncTransferDstType::Texture;
			command.src.file.handle      = runtime_file.file;
			command.src.file.offset      = offset;
			command.src.file.size        = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
			command.dst.texture.resource = resource_allocation.resource;
			command.dst.texture.size     = TextureSize(layout.size.format, mip_size);
			command.dst.texture.offset   = 0;
			command.dst.texture.subresource_index = mip_index;
			
			if (mip_index >= target_mip_level) {
				// SystemWriteToConsole(record_context->alloc, " - MipLevel: %\n"_sl, mip_index);
				AppendAsyncTransferCommand(async_transfer_queue, command);
			}
			
			offset += command.src.file.size;
		}
		
		auto texture_id = record_context->resource_table->AddTransient(resource_allocation.resource, layout.size);
		CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>(texture_id, target_mip_level), descriptor_index);
	}
}

void UpdateTextureStreamingFiles(TextureStreamingSystem* system, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system) {
	extern TextureImportResult ImportTextureFile(StackAllocator* alloc, ThreadPool* thread_pool, const TextureSourceData& source_data, u64 runtime_data_guid);
	
	auto* alloc = record_context->alloc;
	auto* graphics_context = record_context->context;
	for (auto* entity_array : QueryEntities<TextureAssetType>(alloc, *asset_system)) {
		auto streams = ExtractComponentStreams<TextureAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->created_mask)) {
			auto& layout                = streams.runtime_data_layout[i];
			auto& runtime_file          = streams.runtime_file[i];
			auto& resource_allocation   = streams.resource_allocation[i];
			auto& descriptor_allocation = streams.descriptor_allocation[i];
			
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
				
				if (descriptor_allocation.index != u32_max) {
					system->descriptor_index_to_texture_entity_id[descriptor_allocation.index] = EntityID{ u32_max };
					system->streamed_mip_level[descriptor_allocation.index] = 0xFF;
					DeallocatePersistentSrvDescriptor(graphics_context, descriptor_allocation.index);
					descriptor_allocation = {};
				}
				
				auto result = ImportTextureFile(alloc, thread_pool, streams.source_data[i], layout.file_guid);
				if (result.success) layout = result.layout;
			}
			
			// TODO: Add support for unaligned async reads.
			runtime_file.file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..trd"_sl, layout.file_guid), OpenFileFlags::Read /*| OpenFileFlags::Async*/);
			
			descriptor_allocation.index  = AllocatePersistentSrvDescriptor(graphics_context);
			resource_allocation.resource = CreateTextureResource(graphics_context, layout.size, CreateResourceFlags::None);
			
			CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>((VirtualResourceID)0), descriptor_allocation.index);
			system->descriptor_index_to_texture_entity_id[descriptor_allocation.index] = EntityID{ (u32)i };
		}
	}
}
