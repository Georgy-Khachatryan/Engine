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
	
	NativeMemoryResource memory_resource;
	Array<u32> tile_indices;
	
	u64 heap_size = 0;
};

TextureStreamingSystem* CreateTextureStreamingSystem(GraphicsContext* context, StackAllocator* alloc, u64 heap_size) {
	auto* system = NewFromAlloc(alloc, TextureStreamingSystem);
	system->heap_size = heap_size;
	
	ArrayResizeMemset(system->descriptor_index_to_texture_entity_id, alloc, persistent_srv_descriptor_count, 0xFF);
	ArrayResizeMemset(system->streamed_mip_level,                    alloc, persistent_srv_descriptor_count, 0xFF);
	
	system->memory_resource = CreateMemoryResource(context, heap_size);
	
	ArrayReserve(system->tile_indices, alloc, heap_size >> 16);
	for (u32 i = 0; i < system->tile_indices.capacity; i += 1) {
		ArrayAppend(system->tile_indices, i);
	}
	
	return system;
}

void ReleaseTextureStreamingSystem(GraphicsContext* context, TextureStreamingSystem* system) {
	ReleaseMemoryResource(context, system->memory_resource);
}

void UpdateTextureStreamingSystem(TextureStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* texture_streaming_feedback_queue) {
	auto element = texture_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return;
	
	auto streams = ExtractComponentStreams<TextureAssetType>(QueryEntityTypeArray<TextureAssetType>(*asset_system));
	auto* graphics_context = record_context->context;
	auto* alloc = record_context->alloc;
	
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
		
		auto sparse_layout = GetSparseTextureLayout(graphics_context, resource_allocation.resource);
		if (sparse_layout.packed_mip_count != 0) {
			target_mip_level = Math::Min((u32)sparse_layout.regular_mip_count, target_mip_level);
		}
		
		// SystemWriteToConsole(record_context->alloc, "Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, target_mip_level);
		
		u32 last_subresource_index = Math::Min((u32)sparse_layout.regular_mip_count + (sparse_layout.packed_mip_count != 0 ? 1 : 0), streamed_mip_level);
		for (s32 mip_index = last_subresource_index - 1; mip_index >= (s32)target_mip_level; mip_index -= 1) {
			auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
			auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
			
			auto tile_count = mip_size_tiles.x * mip_size_tiles.y;
			if (mip_index >= sparse_layout.regular_mip_count) {
				tile_count = sparse_layout.packed_tile_count;
			}
			
			if (system->tile_indices.count < tile_count) {
				target_mip_level = mip_index + 1u;
				break;
			}
			
			auto tile_indices = ArrayView<u32>{ system->tile_indices.data + system->tile_indices.count - tile_count, tile_count };
			system->tile_indices.count -= tile_count;
			
			// SystemWriteToConsole(record_context->alloc, " - MapMipLevel: % (%)\n"_sl, mip_index, mip_index >= sparse_layout.regular_mip_count ? "Packed"_sl : "Regular"_sl);
			
			AsyncUpdateMemoryMappings(graphics_context, alloc, tile_indices, mip_index, resource_allocation.resource, system->memory_resource);
		}
		system->streamed_mip_level[descriptor_index] = target_mip_level;
		
		auto format = texture_format_info_map[(u32)layout.size.format];
		
		u64 offset = 0;
		for (u32 mip_index = 0; mip_index < Math::Min(streamed_mip_level, (u32)layout.size.mips); mip_index += 1) {
			auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
			auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
			
			AsyncTransferCommand command;
			command.src_type = AsyncTransferSrcType::File;
			command.dst_type = AsyncTransferDstType::Texture;
			command.src.file.handle      = runtime_file.file;
			command.src.file.offset      = offset;
			command.src.file.size        = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
			command.dst.texture.resource = resource_allocation.resource;
			command.dst.texture.size     = TextureSize(layout.size.format, mip_size_texels);
			command.dst.texture.offset   = 0;
			command.dst.texture.subresource_index = mip_index;
			
			if (mip_index >= target_mip_level) {
				// SystemWriteToConsole(record_context->alloc, " - CopyMipLevel: %\n"_sl, mip_index);
				
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
					// TODO: Release tiles.
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
			resource_allocation.resource = CreateTextureResource(graphics_context, layout.size, CreateResourceFlags::Sparse);
			
			CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>((VirtualResourceID)0), descriptor_allocation.index);
			system->descriptor_index_to_texture_entity_id[descriptor_allocation.index] = EntityID{ (u32)i };
		}
	}
}
