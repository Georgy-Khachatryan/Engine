#include "Basic/BasicBitArray.h"
#include "Basic/BasicMemory.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/RecordContext.h"
#include "RendererEntities.h"
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
	u64 state             = 0;
	u64 wait_index[texture_max_mip_level_count] = {};
	
	TextureMipLevelRuntimeState LoadState(u32 mip_index) {
		return (TextureMipLevelRuntimeState)((state >> (mip_index * 4)) & 0xF);
	}
	
	void StoreState(u32 mip_index, TextureMipLevelRuntimeState mip_state) {
		state = (state & ~((u64)0xF << (mip_index * 4))) | ((u64)mip_state << (mip_index * 4));
	}
};

struct TextureMipLevelDeallocationCandidate {
	u32 cache_priority   = 0;
	u32 descriptor_index = 0;
};

struct TextureStreamingSystem {
	Array<EntityID> descriptor_index_to_texture_entity_id;
	Array<TextureRuntimeData> runtime_textures;
	
	Array<u64> tile_states;
	Array<u32> free_tile_indices;
	
	NativeMemoryResource memory_resource;
};

TextureStreamingSystem* CreateTextureStreamingSystem(GraphicsContext* context, StackAllocator* alloc, u64 heap_size) {
	auto* system = NewFromAlloc(alloc, TextureStreamingSystem);
	
	ArrayResizeMemset(system->descriptor_index_to_texture_entity_id, alloc, persistent_srv_descriptor_count, 0xFF);
	ArrayResize(system->runtime_textures, alloc, persistent_srv_descriptor_count);
	
	ArrayResizeMemset(system->tile_states,  alloc, heap_size >> gpu_memory_page_size_bits, 0);
	ArrayReserve(system->free_tile_indices, alloc, heap_size >> gpu_memory_page_size_bits);
	
	for (u32 i = 0; i < system->free_tile_indices.capacity; i += 1) {
		ArrayAppend(system->free_tile_indices, i);
	}
	
	system->memory_resource = CreateMemoryResource(context, heap_size);
	
	return system;
}

void ReleaseTextureStreamingSystem(GraphicsContext* context, TextureStreamingSystem* system) {
	ReleaseMemoryResource(context, system->memory_resource);
}

static u64 ComputeMipLevelOffset(TextureSize size, u32 mip_index) {
	auto format = texture_format_info_map[(u32)size.format];
	
	u64 offset = 0;
	for (u32 i = 0; i < mip_index; i += 1) {
		auto mip_size_texels = Math::Max(uint2(size) >> i, 1u);
		auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
		auto mip_size_bytes  = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
		
		offset += mip_size_bytes;
	}
	
	return offset;
}

void UpdateTextureStreamingSystem(TextureStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* texture_streaming_feedback_queue) {
	auto element = texture_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return;
	
	auto streams = ExtractComponentStreams<TextureAssetType>(QueryEntityTypeArray<TextureAssetType>(*asset_system));
	auto* graphics_context = record_context->context;
	auto* alloc = record_context->alloc;
	
	u64 current_frame_index = record_context->frame_index;
	u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
	
	HashTable<u64, u32> tile_state_to_tile_index;
	HashTableReserve(tile_state_to_tile_index, alloc, system->free_tile_indices.capacity - system->free_tile_indices.count);
	
	for (u32 tile_index = 0; tile_index < system->tile_states.count; tile_index += 1) {
		u64 tile_state = system->tile_states[tile_index];
		if (tile_state == 0) continue;
		
		HashTableAddOrFind(tile_state_to_tile_index, tile_state, tile_index);
	}
	
	u32 in_flight_deallocate_tile_count = 0;
	for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
		auto& runtime_texture = system->runtime_textures[descriptor_index];
		if (runtime_texture.state == 0) continue;
		
		auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
		auto& layout = streams.runtime_data_layout[entity_id.index];
		auto& runtime_file = streams.runtime_file[entity_id.index];
		
		auto& resource_allocation = streams.resource_allocation[entity_id.index];
		auto sparse_layout = GetSparseTextureLayout(graphics_context, resource_allocation.resource);
		auto format = texture_format_info_map[(u32)layout.size.format];
		
		bool should_update_descriptor = false;
		u32 min_ready_mip_index = u32_max;
		
		u32 end_mip_index = sparse_layout.regular_mip_count + (sparse_layout.packed_mip_count != 0 ? 1 : 0);
		
		for (s32 mip_index = end_mip_index - 1; mip_index >= 0; mip_index -= 1) {
			auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
			auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
			auto mip_size_bytes  = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
			auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
			
			u32 tile_count = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_tile_count : mip_size_tiles.x * mip_size_tiles.y;
			
			switch (runtime_texture.LoadState(mip_index)) {
			case TextureMipLevelRuntimeState::Allocate: {
				if (system->free_tile_indices.count >= tile_count) {
					runtime_texture.StoreState(mip_index, TextureMipLevelRuntimeState::FileRead);
					
					auto tile_indices = ArrayView<u32>{ system->free_tile_indices.data + system->free_tile_indices.count - tile_count, tile_count };
					system->free_tile_indices.count -= tile_count;
					
					for (u32 mip_tile_index = 0; mip_tile_index < tile_count; mip_tile_index += 1) {
						u32 tile_index = tile_indices[mip_tile_index];
						system->tile_states[tile_index] = (descriptor_index + 1) | ((u64)mip_index << 32) | ((u64)mip_tile_index << 36);
					}
					
					// SystemWriteToConsole(record_context->alloc, "^ Map, Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, mip_index);
					AsyncUpdateMemoryMappings(graphics_context, alloc, tile_indices, mip_index, resource_allocation.resource, system->memory_resource);
					
					u32 read_count = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_mip_count : 1;
					
					for (u32 i = 0; i < read_count; i += 1) {
						// SystemWriteToConsole(record_context->alloc, "- Read, Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, mip_index + i);
						
						auto mip_size_texels = Math::Max(uint2(layout.size) >> (mip_index + i), 1u);
						auto mip_size_blocks = (mip_size_texels + (uint2(1u) << format.block_size_log2) - 1u) >> format.block_size_log2;
						auto mip_size_bytes  = AlignUp(mip_size_blocks.x * format.block_size_bytes, texture_row_pitch_alignment) * mip_size_blocks.y;
						
						runtime_texture.wait_index[mip_index] = AsyncCopyFileToTexture(
							async_transfer_queue,
							resource_allocation.resource,
							mip_index + i,
							TextureSize(layout.size.format, mip_size_texels),
							runtime_file.file,
							ComputeMipLevelOffset(layout.size, mip_index + i),
							mip_size_bytes
						);
					}
				}
				break;
			} case TextureMipLevelRuntimeState::FileRead: {
				if (runtime_texture.wait_index[mip_index] <= completed_file_read_index) {
					// SystemWriteToConsole(record_context->alloc, "! ReadDone, Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, mip_index);
					
					runtime_texture.StoreState(mip_index, TextureMipLevelRuntimeState::Ready);
					should_update_descriptor = true;
					min_ready_mip_index = mip_index;
				}
				break;
			} case TextureMipLevelRuntimeState::Ready: {
				min_ready_mip_index = mip_index;
				break;
			} case TextureMipLevelRuntimeState::Deallocate: {
				if (runtime_texture.wait_index[mip_index] <= current_frame_index) {
					runtime_texture.StoreState(mip_index, TextureMipLevelRuntimeState::Free);
					should_update_descriptor = true;
					
					// SystemWriteToConsole(record_context->alloc, "v Unmap, Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, mip_index);
					
					for (u32 mip_tile_index = 0; mip_tile_index < tile_count; mip_tile_index += 1) {
						u64 tile_state = (descriptor_index + 1) | ((u64)mip_index << 32) | ((u64)mip_tile_index << 36);
						u32 tile_index = HashTableFind(tile_state_to_tile_index, tile_state)->value;
						ArrayAppend(system->free_tile_indices, tile_index);
						system->tile_states[tile_index] = 0;
					}
				} else {
					in_flight_deallocate_tile_count += tile_count;
				}
				break;
			}
			}
		}
		
		if (should_update_descriptor) {
			if (min_ready_mip_index != u32_max) {
				// SystemWriteToConsole(record_context->alloc, "x UpdateDescriptor, Texture: %:%, MipLevel: %\n"_sl, streams.name[entity_id.index].name, descriptor_index, min_ready_mip_index);
				
				auto texture_id = record_context->resource_table->AddTransient(resource_allocation.resource, layout.size);
				CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>(texture_id, min_ready_mip_index), descriptor_index);
			} else {
				CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>((VirtualResourceID)0), descriptor_index);
			}
		}
	}
	
	Array<u64> requests;
	ArrayReserve(requests, alloc, persistent_srv_descriptor_count);
	
	u32* texture_streaming_feedback_data = (u32*)element.data;
	for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
		u32 packed_feedback = texture_streaming_feedback_data[descriptor_index];
		if (packed_feedback == u32_max) continue;
		
		auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
		auto& runtime_texture = system->runtime_textures[descriptor_index];
		
		auto& layout = streams.runtime_data_layout[entity_id.index];
		auto& resource_allocation = streams.resource_allocation[entity_id.index];
		auto sparse_layout = GetSparseTextureLayout(graphics_context, resource_allocation.resource);
		
		u32 begin_mip_level = (u32)*(float*)&packed_feedback;
		if (sparse_layout.packed_mip_count != 0) {
			begin_mip_level = Math::Min((u32)sparse_layout.regular_mip_count, begin_mip_level);
		}
		u32 end_mip_index = sparse_layout.regular_mip_count + (sparse_layout.packed_mip_count != 0 ? 1 : 0);
		
		for (u32 mip_index = begin_mip_level; mip_index < end_mip_index; mip_index += 1) {
			auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
			auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
			
			u32 tile_count = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_tile_count : mip_size_tiles.x * mip_size_tiles.y;
			
			ArrayAppend(requests, alloc, ((u64)tile_count << 36) | ((u64)(0xF - mip_index) << 32) | descriptor_index);
		}
		
		runtime_texture.cache_mip_index = layout.size.mips;
	}
	
	u32 deallocate_tile_count = 0;
	if (requests.count != 0) {
		HeapSort<u64>(requests);
		
		u32 total_tile_count = (u32)system->free_tile_indices.capacity;
		
		u32 tile_count_for_all_requests = 0;
		u32 allocate_tile_count = 0;
		for (u64 request : requests) {
			u32 tile_count       = (u32)(request >> 36);
			u32 mip_index        = 0xF - (u32)((request >> 32) & 0xF);
			u32 descriptor_index = (u32)request;
			
			auto& runtime_texture = system->runtime_textures[descriptor_index];
			
			tile_count_for_all_requests += tile_count;
			if (tile_count_for_all_requests >= total_tile_count) {
				break;
			}
			
			runtime_texture.cache_frame_index = (u32)current_frame_index;
			runtime_texture.cache_mip_index   = Math::Min(mip_index, runtime_texture.cache_mip_index);
			
			if (runtime_texture.LoadState(mip_index) == TextureMipLevelRuntimeState::Free) {
				runtime_texture.StoreState(mip_index, TextureMipLevelRuntimeState::Allocate);
			}
			
			if (runtime_texture.LoadState(mip_index) == TextureMipLevelRuntimeState::Allocate) {
				allocate_tile_count += tile_count;
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
		Array<TextureMipLevelDeallocationCandidate> deallocation_candidates;
		ArrayReserve(deallocation_candidates, alloc, requests.count);
		
		for (u32 descriptor_index = 0; descriptor_index < persistent_srv_descriptor_count; descriptor_index += 1) {
			auto& runtime_texture = system->runtime_textures[descriptor_index];
			if (runtime_texture.state == 0) continue;
			
			auto entity_id = system->descriptor_index_to_texture_entity_id[descriptor_index];
			
			auto& layout = streams.runtime_data_layout[entity_id.index];
			auto& resource_allocation = streams.resource_allocation[entity_id.index];
			auto sparse_layout = GetSparseTextureLayout(graphics_context, resource_allocation.resource);
			
			for (u32 mip_index = 0; mip_index < (u32)layout.size.mips; mip_index += 1) {
				auto state = runtime_texture.LoadState(mip_index);
				if (state != TextureMipLevelRuntimeState::Ready || (runtime_texture.cache_frame_index == (u32)current_frame_index && mip_index >= runtime_texture.cache_mip_index)) continue;
				
				auto mip_size_texels = Math::Max(uint2(layout.size) >> mip_index, 1u);
				auto mip_size_tiles  = DivideAndRoundUp(mip_size_texels, uint2(sparse_layout.tile_shape));
				
				u32 tile_count = mip_index >= sparse_layout.regular_mip_count ? sparse_layout.packed_tile_count : mip_size_tiles.x * mip_size_tiles.y;
				
				TextureMipLevelDeallocationCandidate candidate;
				candidate.cache_priority   = (tile_count << 4) | (0xF - mip_index);
				candidate.descriptor_index = descriptor_index;
				ArrayAppend(deallocation_candidates, alloc, candidate);
			}
		}
		
		HeapSort<TextureMipLevelDeallocationCandidate>(deallocation_candidates, [](const TextureMipLevelDeallocationCandidate& lh, const TextureMipLevelDeallocationCandidate& rh)-> bool {
			return lh.cache_priority > rh.cache_priority;
		});
		
		u32 deallocated_tile_count = 0;
		for (u64 i = 0; i < deallocation_candidates.count && deallocated_tile_count < deallocate_tile_count; i += 1) {
			auto& candidate = deallocation_candidates[i];
			u32 tile_count = (candidate.cache_priority >> 4);
			u32 mip_index  = 0xF - (candidate.cache_priority & 0xF);
			
			// TODO: Update descriptor here?
			auto& runtime_texture = system->runtime_textures[candidate.descriptor_index];
			runtime_texture.StoreState(mip_index, TextureMipLevelRuntimeState::Deallocate);
			runtime_texture.wait_index[mip_index] = current_frame_index + number_of_frames_in_flight;
			
			deallocate_tile_count += tile_count;
		}
	}
}

static void InvalidateTextureStreaming(TextureStreamingSystem* system, RecordContext* record_context, TextureAssetType streams, u32 texture_asset_index) {
	auto& layout                = streams.runtime_data_layout[texture_asset_index];
	auto& descriptor_allocation = streams.descriptor_allocation[texture_asset_index];
	
	auto& runtime_texture = system->runtime_textures[descriptor_allocation.index];
	for (u32 i = 0; i < (u32)layout.size.mips; i += 1) {
		if ((u32)runtime_texture.LoadState(i) >= (u32)TextureMipLevelRuntimeState::FileRead) {
			runtime_texture.StoreState(i, TextureMipLevelRuntimeState::Deallocate);
			runtime_texture.wait_index[i] = record_context->frame_index + number_of_frames_in_flight;
		}
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
					InvalidateTextureStreaming(system, record_context, streams, (u32)i);
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
			
			resource_allocation.resource = CreateTextureResource(graphics_context, layout.size, CreateResourceFlags::Sparse);
			
			CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>((VirtualResourceID)0), descriptor_allocation.index);
			system->descriptor_index_to_texture_entity_id[descriptor_allocation.index] = EntityID{ (u32)i };
		}
	}
}
