#include "MeshStreamingSystem.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "RenderPasses.h"
#include "GraphicsApi/RecordContext.h"
#include "GraphicsApi/AsyncTransferQueue.h"

enum struct MeshRuntimeState : u32 {
	Free       = 0,
	Allocate   = 1,
	FileRead   = 2,
	Ready      = 3,
	Deallocate = 4,
};

struct MeshAssetRuntimeData {
	u32 mesh_asset_index  = 0;
	u32 cache_frame_index = 0;
	u32 cache_priority    = 0;
	MeshRuntimeState state = MeshRuntimeState::Free;
	u64 wait_index = 0;
};

struct MeshDeallocationCandidate {
	u32 cache_priority     = 0;
	u32 runtime_mesh_index = 0;
};

struct MeshStreamingSystem {
	NumaHeapAllocator heap;
	u64 total_allocated_size = 0;
	
	Array<MeshAssetRuntimeData> runtime_meshes;
	Array<u32> free_mesh_indices;
};

compile_const u32 max_runtime_mesh_asset_count = 1024;

MeshStreamingSystem* CreateMeshStreamingSystem(StackAllocator* alloc, u64 buffer_size) {
	auto* system = NewFromAlloc(alloc, MeshStreamingSystem);
	
	ArrayResize(system->runtime_meshes, alloc, max_runtime_mesh_asset_count);
	
	ArrayReserve(system->free_mesh_indices, alloc, max_runtime_mesh_asset_count);
	for (u32 i = 0; i < max_runtime_mesh_asset_count; i += 1) {
		ArrayAppend(system->free_mesh_indices, max_runtime_mesh_asset_count - i - 1);
	}
	
	system->heap = CreateNumaHeapAllocator(alloc, max_runtime_mesh_asset_count, (u32)buffer_size);
	
	return system;
}

static ArrayView<u64> ProcessMeshStreamingFeedback(RecordContext* record_context, GpuReadbackQueue* mesh_streaming_feedback_queue) {
	auto element = mesh_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return {};
	
	u32 read_index = 0;
	u32* meshlet_streaming_feedback_data = (u32*)element.data;
	
	u32 size = meshlet_streaming_feedback_data[read_index++];
	
	Array<u64> requests;
	ArrayReserve(requests, record_context->alloc, size - 1);
	
	while (read_index < size) {
		u32 mesh_asset_index = read_index - 1;
		u32 distance = meshlet_streaming_feedback_data[read_index++];
		
		if (distance != u32_max) {
			ArrayAppend(requests, ((u64)distance << 32) | mesh_asset_index);
		}
	}
	
	return requests;
}

static u32 ComputeRuntimeMeshSize(const MeshRuntimeDataLayout& layout) {
	compile_const u32 page_residency_mask_size = (MeshletPageHeader::max_page_count / 32u) * sizeof(u32);
	u32 file_data_size  = layout.meshlet_group_count * sizeof(MeshletGroup) + page_residency_mask_size;
	u32 allocation_size = file_data_size + layout.page_count * sizeof(u32);
	
	return AlignUp(allocation_size, 4096u);
}

void UpdateMeshStreamingSystem(MeshStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* mesh_streaming_feedback_queue) {
	auto entity_array = QueryEntityTypeArray<MeshAssetType>(*asset_system);
	auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
	
	auto& mesh_asset_buffer = GetVirtualResource(record_context, VirtualResourceID::MeshAssetBuffer);
	
	auto* alloc = record_context->alloc;
	auto requests = ProcessMeshStreamingFeedback(record_context, mesh_streaming_feedback_queue);
	
	auto& runtime_meshes = system->runtime_meshes;
	auto& free_mesh_indices = system->free_mesh_indices;
	
	u64 in_flight_deallocate_memory_size = 0;
	u64 current_frame_index = record_context->frame_index;
	u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
	
	compile_const u32 base_allocation_offset = MeshletPageHeader::page_size * MeshletPageHeader::runtime_page_count;
	u64 buffer_size = mesh_asset_buffer.buffer.size - base_allocation_offset;
	
	// Update mesh states:
	for (u32 runtime_mesh_index = 0; runtime_mesh_index < runtime_meshes.count; runtime_mesh_index += 1) {
		auto& mesh = runtime_meshes[runtime_mesh_index];
		
		switch (mesh.state) {
		case MeshRuntimeState::Allocate: {
			auto& layout = streams.runtime_data_layout[mesh.mesh_asset_index];
			u32 mesh_size = ComputeRuntimeMeshSize(layout);
			
			auto heap_allocation = system->heap.Allocate(mesh_size);
			if (heap_allocation.index != u32_max) {
				mesh.state = MeshRuntimeState::FileRead;
				
				system->total_allocated_size += system->heap.GetMemoryBlockSize(heap_allocation);
				
				auto& allocation = streams.allocation[mesh.mesh_asset_index];
				allocation.allocation = heap_allocation;
				allocation.offset     = u32_max;
				
				mesh.wait_index = AsyncCopyFileToBuffer(
					async_transfer_queue,
					mesh_asset_buffer.buffer.resource,
					system->heap.GetMemoryBlockOffset(heap_allocation) + base_allocation_offset,
					mesh_asset_buffer.buffer.size,
					streams.runtime_file[mesh.mesh_asset_index].file,
					layout.page_count * MeshletPageHeader::page_size,
					mesh_size
				);
			}
			break;
		} case MeshRuntimeState::FileRead: {
			if (mesh.wait_index <= completed_file_read_index) {
				mesh.state = MeshRuntimeState::Ready;
				
				auto& allocation = streams.allocation[mesh.mesh_asset_index];
				allocation.offset = (u32)(system->heap.GetMemoryBlockOffset(allocation.allocation) + base_allocation_offset);
				
				BitArraySetBit(entity_array->dirty_mask, mesh.mesh_asset_index);
			}
			break;
		} case MeshRuntimeState::Deallocate: {
			if (mesh.wait_index <= current_frame_index) {
				auto& allocation = streams.allocation[mesh.mesh_asset_index];
				system->total_allocated_size -= system->heap.GetMemoryBlockSize(allocation.allocation);
				system->heap.Deallocate(allocation.allocation);
				allocation = {};
				
				mesh = {};
				ArrayAppend(free_mesh_indices, runtime_mesh_index);
			} else {
				auto& layout = streams.runtime_data_layout[mesh.mesh_asset_index];
				in_flight_deallocate_memory_size += ComputeRuntimeMeshSize(layout);
			}
			break;
		}
		}
		
	}
	
	u64 deallocate_memory_size = 0;
	if (requests.count != 0) {
		TempAllocationScope(alloc);
		
		HashTable<u32, u32> mesh_asset_id_to_index;
		HashTableReserve(mesh_asset_id_to_index, alloc, max_runtime_mesh_asset_count);
		
		for (u32 runtime_mesh_index = 0; runtime_mesh_index < runtime_meshes.count; runtime_mesh_index += 1) {
			auto& mesh = runtime_meshes[runtime_mesh_index];
			if (mesh.state != MeshRuntimeState::Free) {
				HashTableAddOrFind(mesh_asset_id_to_index, mesh.mesh_asset_index, runtime_mesh_index);
			}
		}
		
		// Process mesh requests in order of priority. Only the first X meshes that fit in
		// the preallocated buffer would be updated and will become eligible for allocation.
		HeapSort(requests);
		
		u64 memory_size_for_all_requests = 0;
		u64 allocate_memory_size = 0;
		for (u64 packed_request : requests) {
			u32 mesh_asset_index = (u32)packed_request;
			auto* element = HashTableFind(mesh_asset_id_to_index, mesh_asset_index);
			
			u32 runtime_mesh_index = 0;
			if (element != nullptr) {
				runtime_mesh_index = element->value;
			} else if (free_mesh_indices.count != 0) {
				runtime_mesh_index = ArrayPopLast(free_mesh_indices);
				
				auto& mesh = runtime_meshes[runtime_mesh_index];
				mesh.state             = MeshRuntimeState::Allocate;
				mesh.mesh_asset_index  = mesh_asset_index;
			} else {
				continue;
			}
			
			u32 mesh_size = ComputeRuntimeMeshSize(streams.runtime_data_layout[mesh_asset_index]);
			
			memory_size_for_all_requests += mesh_size;
			if (memory_size_for_all_requests > buffer_size) {
				// After overflow we know that no more meshes will fit. Break here so their frame
				// index and priority are not updated and they become eligible for deallocation.
				break;
			}
			
			auto& mesh = runtime_meshes[runtime_mesh_index];
			mesh.cache_frame_index = (u32)current_frame_index;
			mesh.cache_priority    = (u32)(packed_request >> 32);
			
			if (mesh.state == MeshRuntimeState::Allocate) {
				allocate_memory_size += mesh_size;
			}
		}
		
		// If we overflow the buffer start deallocating unused or low priority meshes.
		u64 required_memory_size = system->total_allocated_size + allocate_memory_size;
		if (required_memory_size > buffer_size) {
			deallocate_memory_size = required_memory_size - buffer_size;
		}
		
		// Make sure we account for any in flight deallocations, we can expect them to be ready soon.
		deallocate_memory_size -= Math::Min(in_flight_deallocate_memory_size, deallocate_memory_size);
	}
	
	// Try to deallocate some unused or low priority meshes to make space for new meshes.
	if (deallocate_memory_size != 0) {
		Array<MeshDeallocationCandidate> deallocation_candidates;
		ArrayReserve(deallocation_candidates, alloc, runtime_meshes.count);
		
		for (u32 runtime_mesh_index = 0; runtime_mesh_index < runtime_meshes.count; runtime_mesh_index += 1) {
			auto& mesh = runtime_meshes[runtime_mesh_index];
			
			if (mesh.state != MeshRuntimeState::Ready || mesh.cache_frame_index == (u32)current_frame_index) continue;
			
			MeshDeallocationCandidate candidate;
			candidate.cache_priority     = mesh.cache_priority;
			candidate.runtime_mesh_index = runtime_mesh_index;
			ArrayAppend(deallocation_candidates, candidate);
		}
		
		HeapSort<MeshDeallocationCandidate>(deallocation_candidates, [](const MeshDeallocationCandidate& lh, const MeshDeallocationCandidate& rh)-> bool {
			return lh.cache_priority > rh.cache_priority;
		});
		
		u64 deallocated_memory_size = 0;
		for (u64 i = 0; i < deallocation_candidates.count && deallocated_memory_size < deallocate_memory_size; i += 1) {
			auto& candidate = deallocation_candidates[i];
			auto& mesh = runtime_meshes[candidate.runtime_mesh_index];
			mesh.state      = MeshRuntimeState::Deallocate;
			mesh.wait_index = current_frame_index + number_of_frames_in_flight;
			
			deallocated_memory_size += ComputeRuntimeMeshSize(streams.runtime_data_layout[mesh.mesh_asset_index]);
			
			auto& allocation = streams.allocation[mesh.mesh_asset_index];
			allocation.offset = u32_max;
			
			BitArraySetBit(entity_array->dirty_mask, mesh.mesh_asset_index);
		}
	}
}

static void InvalidateMeshStreaming(MeshStreamingSystem* system, RecordContext* record_context, EntityTypeArray* entity_array, MeshAssetType streams, u32 mesh_asset_index) {
	for (auto& mesh : system->runtime_meshes) {
		if (mesh.state == MeshRuntimeState::Free || mesh.mesh_asset_index != mesh_asset_index) continue;
		
		mesh.state      = MeshRuntimeState::Deallocate;
		mesh.wait_index = record_context->frame_index + number_of_frames_in_flight;
	}
	
	auto& allocation = streams.allocation[mesh_asset_index];
	allocation.offset = u32_max;
	
	BitArraySetBit(entity_array->dirty_mask, mesh_asset_index);
}

void UpdateMeshStreamingFiles(MeshStreamingSystem* system, RecordContext* record_context, AssetEntitySystem* asset_system) {
	extern MeshImportResult ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid);
	
	auto* alloc = record_context->alloc;
	for (auto* entity_array : QueryEntities<MeshAssetType>(alloc, *asset_system)) {
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->created_mask)) {
			auto& layout = streams.runtime_data_layout[i];
			auto& allocation = streams.allocation[i];
			auto& runtime_file = streams.runtime_file[i];
			
			if (layout.version != MeshRuntimeDataLayout::current_version) {
				if (layout.file_guid == 0) {
					layout.file_guid = GenerateRandomNumber64(asset_system->guid_random_seed);
				}
				
				if (runtime_file.file.handle != nullptr) {
					InvalidateMeshStreaming(system, record_context, entity_array, streams, (u32)i);
					SystemCloseFile(runtime_file.file);
				}
				
				auto result = ImportFbxMeshFile(alloc, streams.source_data[i].filepath, layout.file_guid);
				if (result.success) {
					layout = result.layout;
					
					auto& aabb = streams.aabb[i];
					aabb.min = result.aabb_min;
					aabb.max = result.aabb_max;
				}
			}
			
			runtime_file.file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, layout.file_guid), OpenFileFlags::Read | OpenFileFlags::Async);
		}
	}
}

