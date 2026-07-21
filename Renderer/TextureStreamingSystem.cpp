#include "Basic/BasicBitArray.h"
#include "Basic/BasicMemory.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/RecordContext.h"
#include "RendererEntities.h"
#include "StreamingSystem.h"
#include "TextureAsset.h"
#include "TextureStreamingSystem.h"

enum struct TextureMipLevelRuntimeState : u32 {
	Free       = 0,
	Allocate   = 1,
	FileRead   = 2,
	Ready      = 3,
	Deallocate = 4,
};

struct TextureRuntimeData {
	u32 cache_frame_index = 0;
	u32 cache_mip_index   = 0;
	u64 packed_states     = 0;
	u64 wait_index[texture_max_mip_level_count] = {};
	
	TextureMipLevelRuntimeState LoadState(u32 mip_index) {
		return (TextureMipLevelRuntimeState)((packed_states >> (mip_index * 4)) & 0xF);
	}
	
	void StoreState(u32 mip_index, TextureMipLevelRuntimeState mip_state) {
		packed_states = (packed_states & ~((u64)0xF << (mip_index * 4))) | ((u64)mip_state << (mip_index * 4));
	}
};

enum struct TextureRuntimeTileState : u16 {
	Free  = 0,
	Ready = 1,
};

struct alignas(u64) TextureRuntimeTileData {
	TextureRuntimeTileState state = TextureRuntimeTileState::Free;
	u16 descriptor_index = 0;
	u16 mip_tile_index   = 0;
	u16 mip_index        = 0;
};

struct TextureStreamingSystem {
	Array<EntityID> descriptor_index_to_texture_entity_id;
	Array<TextureRuntimeData> runtime_textures;
	
	Array<TextureRuntimeTileData> runtime_tiles;
	Array<u32> free_tile_indices;
	
	NativeMemoryResource memory_resource;
};

struct TextureSubresourceID {
	u32 descriptor_index = 0;
	u32 mip_index        = 0;
	u32 tile_count       = 0;
};

static u64 EncodeTextureSubresourceID(u32 descriptor_index, u32 mip_index, u32 tile_count) {
	return (u64)descriptor_index | ((0xF - (u64)mip_index) << 32) | ((u64)tile_count << 36);
}

static TextureSubresourceID DecodeTextureSubresourceID(u64 encoded) {
	return { (u32)encoded, 0xF - (u32)(encoded >> 32) & 0xF, (u32)(encoded >> 36) };
}

TextureStreamingSystem* CreateTextureStreamingSystem(GraphicsContext* context, StackAllocator* alloc, u64 heap_size) {
	auto* system = NewFromAlloc(alloc, TextureStreamingSystem);
	
	ArrayResizeMemset(system->descriptor_index_to_texture_entity_id, alloc, persistent_srv_descriptor_count, 0xFF);
	ArrayResize(system->runtime_textures, alloc, persistent_srv_descriptor_count);
	
	ArrayResizeMemset(system->runtime_tiles, alloc, heap_size >> gpu_memory_page_size_bits, 0);
	ArrayReserve(system->free_tile_indices,  alloc, heap_size >> gpu_memory_page_size_bits);
	
	for (u32 i = 0; i < system->free_tile_indices.capacity; i += 1) {
		ArrayAppend(system->free_tile_indices, i);
	}
	
	system->memory_resource = CreateMemoryResource(context, heap_size);
	
	return system;
}

void ReleaseTextureStreamingSystem(GraphicsContext* context, TextureStreamingSystem* system) {
	ReleaseMemoryResource(context, system->memory_resource);
}

static u64 ComputeMipLevelOffset(TextureSize size, u32 mip_index, const TextureFormatInfo& format) {
	u64 offset = 0;
	
	for (u32 i = 0; i < mip_index; i += 1) {
		auto mip_size_texels = Math::Max(uint2(size) >> i, 1u);
		auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
		auto mip_size_bytes  = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
		
		offset += mip_size_bytes;
	}
	
	return offset;
}

static ArrayView<u64> ProcessTextureStreamingFeedback(RecordContext* record_context, GpuReadbackQueue* texture_streaming_feedback_queue, TextureAssetType streams, ArrayView<EntityID> descriptor_index_to_texture_entity_id) {
	ProfilerScope("ProcessTextureStreamingFeedback");
	
	auto element = texture_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return {};
	
	auto* alloc = record_context->alloc;
	
	Array<u64> requests;
	ArrayReserve(requests, alloc, persistent_srv_descriptor_count);
	
	u32* texture_streaming_feedback_data = (u32*)element.data;
	for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
		u32 packed_feedback = texture_streaming_feedback_data[descriptor_index];
		if (packed_feedback == u32_max) continue;
		
		auto entity_id = descriptor_index_to_texture_entity_id[descriptor_index];
		
		auto& layout = streams.runtime_data_layout[entity_id.index];
		auto sparse_layout = streams.resource_allocation[entity_id.index].sparse_layout;
		
		u32 begin_mip_level = (u32)*(float*)&packed_feedback;
		if (sparse_layout.packed_mip_count != 0) {
			begin_mip_level = Math::Min((u32)sparse_layout.regular_mip_count, begin_mip_level);
		}
		u32 end_mip_index = sparse_layout.regular_mip_count + (sparse_layout.packed_mip_count != 0 ? 1 : 0);
		
		for (u32 mip_index = begin_mip_level; mip_index < end_mip_index; mip_index += 1) {
			auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
			auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
			
			u32 tile_count = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_tile_count : mip_size_tiles.x * mip_size_tiles.y;
			
			ArrayAppend(requests, alloc, EncodeTextureSubresourceID(descriptor_index, mip_index, tile_count));
		}
	}
	
	return requests;
}

void UpdateTextureStreamingSystem(TextureStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* texture_streaming_feedback_queue) {
	ProfilerScope("UpdateTextureStreamingSystem");
	
	auto* graphics_context = record_context->context;
	auto* alloc = record_context->alloc;
	
	auto* entity_array = QueryEntityTypeArray<TextureAssetType>(*asset_system);
	auto streams = ExtractComponentStreams<TextureAssetType>(entity_array);
	
	auto requests = ProcessTextureStreamingFeedback(record_context, texture_streaming_feedback_queue, streams, system->descriptor_index_to_texture_entity_id);
	
	u64 current_frame_index = record_context->frame_index;
	u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
	
	u32 in_flight_deallocate_tile_count = 0;
	for (u32 runtime_tile_index = 0; runtime_tile_index < system->runtime_tiles.count; runtime_tile_index += 1) {
		auto tile = system->runtime_tiles[runtime_tile_index];
		if (tile.state == TextureRuntimeTileState::Free) continue;
		
		auto& texture = system->runtime_textures[tile.descriptor_index];
		if (texture.LoadState(tile.mip_index) == TextureMipLevelRuntimeState::Deallocate) {
			if (IsWaitComplete(texture.wait_index[tile.mip_index], current_frame_index, completed_file_read_index)) {
				ArrayAppend(system->free_tile_indices, runtime_tile_index);
				system->runtime_tiles[runtime_tile_index] = {};
			} else {
				in_flight_deallocate_tile_count += 1;
			}
		}
	}
	
	for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
		auto& texture = system->runtime_textures[descriptor_index];
		if (texture.packed_states == 0) continue;
		
		auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
		
		auto& layout = streams.runtime_data_layout[entity_id.index];
		auto& resource_allocation = streams.resource_allocation[entity_id.index];
		auto sparse_layout = resource_allocation.sparse_layout;
		
		u32 end_mip_index = sparse_layout.regular_mip_count + (sparse_layout.packed_mip_count != 0 ? 1 : 0);
		for (s32 mip_index = end_mip_index - 1; mip_index >= 0; mip_index -= 1) {
			if (texture.LoadState(mip_index) == TextureMipLevelRuntimeState::Allocate) {
				auto format = texture_format_info_map[(u32)layout.size.format];
				
				u32 tile_count = sparse_layout.packed_tile_count;
				if (mip_index < sparse_layout.regular_mip_count) {
					auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
					auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
					auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
					
					tile_count = mip_size_tiles.x * mip_size_tiles.y;
				}
				
				if (system->free_tile_indices.count >= tile_count) {
					texture.StoreState(mip_index, TextureMipLevelRuntimeState::FileRead);
					
					auto tile_indices = ArrayView<u32>{ system->free_tile_indices.data + system->free_tile_indices.count - tile_count, tile_count };
					system->free_tile_indices.count -= tile_count;
					
					for (u32 mip_tile_index = 0; mip_tile_index < tile_count; mip_tile_index += 1) {
						auto& tile = system->runtime_tiles[tile_indices[mip_tile_index]];
						tile.descriptor_index = descriptor_index;
						tile.mip_index        = mip_index;
						tile.mip_tile_index   = mip_tile_index;
						tile.state            = TextureRuntimeTileState::Ready;
					}
					
					AsyncUpdateMemoryMappings(graphics_context, alloc, tile_indices, mip_index, resource_allocation.resource, system->memory_resource);
					
					u32 mip_count_to_read = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_mip_count : 1;
					
					auto& runtime_file = streams.runtime_file[entity_id.index];
					for (u32 i = 0; i < mip_count_to_read; i += 1) {
						auto mip_size_texels = Math::Max(uint2(layout.size) >> (mip_index + i), 1u);
						auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
						auto mip_size_bytes  = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
						
						u64 file_read_index = AsyncCopyFileToTexture(
							async_transfer_queue,
							resource_allocation.resource,
							mip_index + i,
							TextureSize(layout.size.format, mip_size_texels),
							runtime_file.file,
							ComputeMipLevelOffset(layout.size, mip_index + i, format),
							mip_size_bytes
						);
						
						texture.wait_index[mip_index] = EncodeFileReadWaitIndex(file_read_index);
					}
				}
			}
			
			if (texture.LoadState(mip_index) == TextureMipLevelRuntimeState::FileRead && IsWaitComplete(texture.wait_index[mip_index], current_frame_index, completed_file_read_index)) {
				texture.wait_index[mip_index] = 0;
				texture.StoreState(mip_index, TextureMipLevelRuntimeState::Ready);
				streams.descriptor_allocation[entity_id.index].mip_level_mask |= (1u << mip_index);
				BitArraySetBit(entity_array->dirty_mask, entity_id.index);
			}
			
			if (texture.LoadState(mip_index) == TextureMipLevelRuntimeState::Deallocate && IsWaitComplete(texture.wait_index[mip_index], current_frame_index, completed_file_read_index)) {
				texture.wait_index[mip_index] = 0;
				texture.StoreState(mip_index, TextureMipLevelRuntimeState::Free);
			}
		}
	}
	
	
	u32 deallocate_tile_count = 0;
	if (requests.count != 0) {
		HeapSort(requests);
		
		u32 total_tile_count = (u32)system->free_tile_indices.capacity;
		
		u32 tile_count_for_all_requests = 0;
		u32 allocate_tile_count = 0;
		for (u64 i = 0; (i < requests.count) && (tile_count_for_all_requests < total_tile_count); i += 1) {
			auto [descriptor_index, mip_index, tile_count] = DecodeTextureSubresourceID(requests[i]);
			
			auto& texture = system->runtime_textures[descriptor_index];
			tile_count_for_all_requests += tile_count;
			
			if (tile_count_for_all_requests <= total_tile_count) {
				texture.cache_frame_index = (u32)current_frame_index;
				texture.cache_mip_index   = mip_index; // The highest detail MIP level is always set last because requests are sorted.
				
				if (texture.LoadState(mip_index) == TextureMipLevelRuntimeState::Free) {
					texture.StoreState(mip_index, TextureMipLevelRuntimeState::Allocate);
				}
				
				if (texture.LoadState(mip_index) == TextureMipLevelRuntimeState::Allocate) {
					allocate_tile_count += tile_count;
				}
			}
		}
		
		u32 total_allocated_tile_count = total_tile_count - (u32)system->free_tile_indices.count;
		u32 required_tile_count = total_allocated_tile_count + allocate_tile_count;
		if (required_tile_count > total_tile_count) {
			deallocate_tile_count = required_tile_count - total_tile_count;
		}
		
		deallocate_tile_count -= Math::Min(in_flight_deallocate_tile_count, deallocate_tile_count);
	}
	
	if (deallocate_tile_count != 0) {
		Array<u64> deallocation_candidates;
		ArrayReserve(deallocation_candidates, alloc, requests.count);
		
		for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
			auto& texture = system->runtime_textures[descriptor_index];
			if (texture.packed_states == 0) continue;
			
			auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
			
			auto& layout = streams.runtime_data_layout[entity_id.index];
			auto sparse_layout = streams.resource_allocation[entity_id.index].sparse_layout;
			
			for (u32 mip_index = 0; mip_index < (u32)layout.size.mips; mip_index += 1) {
				auto state = texture.LoadState(mip_index);
				if (state != TextureMipLevelRuntimeState::Ready || (texture.cache_frame_index == (u32)current_frame_index && mip_index >= texture.cache_mip_index)) continue;
				
				auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
				auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
				
				u32 tile_count = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_tile_count : mip_size_tiles.x * mip_size_tiles.y;
				
				ArrayAppend(deallocation_candidates, alloc, EncodeTextureSubresourceID(descriptor_index, mip_index, tile_count));
			}
		}
		
		HeapSort<u64>(deallocation_candidates, [](u64 lh, u64 rh)-> bool {
			return lh > rh;
		});
		
		u32 deallocated_tile_count = 0;
		for (u64 i = 0; i < deallocation_candidates.count && deallocated_tile_count < deallocate_tile_count; i += 1) {
			auto [descriptor_index, mip_index, tile_count] = DecodeTextureSubresourceID(deallocation_candidates[i]);
			auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
			
			auto& texture = system->runtime_textures[descriptor_index];
			texture.StoreState(mip_index, TextureMipLevelRuntimeState::Deallocate);
			texture.wait_index[mip_index] = EncodeGpuFrameWaitIndex(current_frame_index);
			streams.descriptor_allocation[entity_id.index].mip_level_mask &= ~(1u << mip_index);
			
			deallocate_tile_count += tile_count;
			
			BitArraySetBit(entity_array->dirty_mask, entity_id.index);
		}
	}
}

static void InvalidateTextureStreaming(TextureStreamingSystem* system, RecordContext* record_context, EntityTypeArray* entity_array, TextureAssetType streams, u32 texture_asset_index) {
	auto& layout                = streams.runtime_data_layout[texture_asset_index];
	auto& descriptor_allocation = streams.descriptor_allocation[texture_asset_index];
	
	auto& texture = system->runtime_textures[descriptor_allocation.index];
	for (u32 mip_index = 0; mip_index < (u32)layout.size.mips; mip_index += 1) {
		// See @InvalidateDuringFileRead for reference.
		if (texture.LoadState(mip_index) != TextureMipLevelRuntimeState::FileRead) {
			texture.wait_index[mip_index] = EncodeGpuFrameWaitIndex(record_context->frame_index);
		}
		texture.StoreState(mip_index, TextureMipLevelRuntimeState::Deallocate);
	}
	descriptor_allocation.mip_level_mask = 0;
	
	BitArraySetBit(entity_array->dirty_mask, texture_asset_index);
}

void UpdateTextureStreamingFiles(TextureStreamingSystem* system, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system) {
	ProfilerScope("UpdateTextureStreamingFiles");
	
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
					InvalidateTextureStreaming(system, record_context, entity_array, streams, (u32)i);
					SystemCloseFile(runtime_file.file);
					runtime_file = {};
				}
				
				if (resource_allocation.resource.handle != nullptr) {
					ReleaseTextureResource(graphics_context, resource_allocation.resource, ResourceReleaseCondition::EndOfThisGpuFrame);
					resource_allocation = {};
				}
				
				auto result = ImportTextureFile(alloc, thread_pool, streams.source_data[i], layout.file_guid);
				if (result.success) layout = result.layout;
			}
			
			// TODO: Add support for unaligned async reads.
			runtime_file.file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..trd"_sl, layout.file_guid), OpenFileFlags::Read /*| OpenFileFlags::Async*/);
			
			if (descriptor_allocation.index == u32_max) {
				descriptor_allocation.index = AllocatePersistentSrvDescriptor(graphics_context);
			}
			
			if (runtime_file.file.handle != nullptr) {
				resource_allocation.resource = CreateTextureResource(graphics_context, layout.size, CreateResourceFlags::Sparse);
				resource_allocation.sparse_layout = GetSparseTextureLayout(graphics_context, resource_allocation.resource);
			}
			
			system->descriptor_index_to_texture_entity_id[descriptor_allocation.index] = EntityID{ (u32)i };
		}
	}
}
