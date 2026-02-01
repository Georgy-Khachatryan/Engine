#include "MeshletStreamingSystem.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "RenderPasses.h"
#include "GraphicsApi/RecordContext.h"
#include "GraphicsApi/AsyncTransferQueue.h"

enum struct MeshletRuntimePageState : u32 {
	Free     = 0,
	FileRead = 1,
	PageIn   = 2,
	PageOut  = 3,
	Ready    = 4,
};

struct MeshletRuntimePage {
	u32 mesh_asset_index = 0;
	u32 asset_page_index = 0;
	u32 cache_frame_index = 0;
	
	MeshletRuntimePageState state = MeshletRuntimePageState::Free;
	u64 wait_index = 0;
};

struct MeshletPageOutCandidate {
	u32 asset_page_index   = 0;
	u32 runtime_page_index = 0;
};

struct MeshletStreamingSystem {
	NumaHeapAllocator heap;
	
	Array<MeshletRuntimePage> runtime_pages;
	Array<u32> free_page_indices;
	
	ArrayView<MeshletRuntimePageUpdateCommand> page_table_update_commands;
};

MeshletStreamingSystem* CreateMeshletStreamingSystem(StackAllocator* alloc, u64 buffer_size) {
	auto* system = NewFromAlloc(alloc, MeshletStreamingSystem);
	
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	ArrayResize(system->runtime_pages, alloc, runtime_page_count);
	
	ArrayReserve(system->free_page_indices, alloc, runtime_page_count);
	for (u32 i = 0; i < runtime_page_count; i += 1) {
		ArrayAppend(system->free_page_indices, runtime_page_count - i - 1);
	}
	
	system->heap = CreateNumaHeapAllocator(alloc, 1024, (u32)buffer_size);
	
	return system;
}

static u64 EncodeMeshletSubresourceID(u32 mesh_asset_index, u32 asset_page_index) {
	return ((u64)asset_page_index << 32) | mesh_asset_index;
}

static ArrayView<u64> ProcessMeshletStreamingFeedback(RecordContext* record_context, GpuReadbackQueue* meshlet_streaming_feedback_queue) {
	auto element = meshlet_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data == nullptr) return {};
	
	u32 read_index = 0;
	u32* meshlet_streaming_feedback_data = (u32*)element.data;
	
	u32 size = meshlet_streaming_feedback_data[read_index++];
	u32 total_page_count = meshlet_streaming_feedback_data[read_index++];
	
	Array<u64> requests;
	ArrayReserve(requests, record_context->alloc, total_page_count);
	
	while (read_index < size) {
		u32 mesh_asset_index     = meshlet_streaming_feedback_data[read_index++];
		u32 feedback_buffer_size = meshlet_streaming_feedback_data[read_index++];
		
		for (u32 i = 0; i < feedback_buffer_size; i += 1) {
			u32 page_mask = meshlet_streaming_feedback_data[read_index++];
			
			for (u32 bit_index : BitScanLow32(page_mask)) {
				u32 asset_page_index = i * 32u + bit_index;
				ArrayAppend(requests, EncodeMeshletSubresourceID(mesh_asset_index, asset_page_index));
			}
		}
	}
	
	return requests;
}

void UpdateMeshletStreamingSystem(MeshletStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* meshlet_streaming_feedback_queue) {
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	auto* alloc = record_context->alloc;
	
	auto requests = ProcessMeshletStreamingFeedback(record_context, meshlet_streaming_feedback_queue);
	
	auto& runtime_pages = system->runtime_pages;
	auto& free_page_indices = system->free_page_indices;
	
	Array<MeshletRuntimePageUpdateCommand> page_table_update_commands;
	ArrayReserve(page_table_update_commands, alloc, runtime_page_count);
	
	u64 in_flight_page_out_count = 0;
	u64 current_frame_index = record_context->frame_index;
	u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
	
	
	// Update page states:
	for (u32 runtime_page_index = 0; runtime_page_index < runtime_pages.count; runtime_page_index += 1) {
		auto& page = runtime_pages[runtime_page_index];
		
		switch (page.state) {
		case MeshletRuntimePageState::FileRead: {
			if (page.wait_index <= completed_file_read_index) {
				page.state      = MeshletRuntimePageState::PageIn;
				page.wait_index = current_frame_index + number_of_frames_in_flight;
				
				MeshletRuntimePageUpdateCommand page_table_update_command;
				page_table_update_command.type               = MeshletPageTableUpdateCommandType::PageIn;
				page_table_update_command.runtime_page_index = runtime_page_index;
				page_table_update_command.mesh_asset_index   = page.mesh_asset_index;
				page_table_update_command.asset_page_index   = page.asset_page_index;
				ArrayAppend(page_table_update_commands, page_table_update_command);
			}
			break;
		} case MeshletRuntimePageState::PageIn: {
			if (page.wait_index <= current_frame_index) {
				page.state      = MeshletRuntimePageState::Ready;
				page.wait_index = 0;
				page.cache_frame_index = (u32)current_frame_index;
			}
			break;
		} case MeshletRuntimePageState::PageOut: {
			if (page.wait_index <= current_frame_index) {
				page = {};
				ArrayAppend(free_page_indices, runtime_page_index);
			} else {
				in_flight_page_out_count += 1;
			}
			break;
		}
		}
	}
	
	
	// Update cache frame indices of already allocated pages and remove them from the request array.
	if (requests.count != 0) {
		TempAllocationScope(alloc);
		
		HashTable<u64, u32> allocated_page_subresource_id_to_index;
		HashTableReserve(allocated_page_subresource_id_to_index, alloc, runtime_page_count);
		
		for (u32 runtime_page_index = 0; runtime_page_index < runtime_pages.count; runtime_page_index += 1) {
			auto& page = runtime_pages[runtime_page_index];
			if (page.state != MeshletRuntimePageState::Free) {
				u32 runtime_page_index_for_cache_update = page.state == MeshletRuntimePageState::Ready ? runtime_page_index : u32_max;
				u64 subresource_id = EncodeMeshletSubresourceID(page.mesh_asset_index, page.asset_page_index);
				
				HashTableAddOrFind(allocated_page_subresource_id_to_index, subresource_id, runtime_page_index_for_cache_update);
			}
		}
		
		for (u32 i = 0; i < requests.count;) {
			u64 subresource_id = requests[i];
			auto* element = HashTableFind(allocated_page_subresource_id_to_index, subresource_id);
			
			if (element != nullptr) {
				if (element->value != u32_max) {
					runtime_pages[element->value].cache_frame_index = (u32)current_frame_index;
				}
				ArrayEraseSwapLast(requests, i);
			} else {
				i += 1;
			}
		}
	}
	
	
	// At this point we only have requests that don't already have an allocated page. Remove any page requests that won't fit.
	u64 page_out_count = 0;
	if (requests.count > free_page_indices.count) {
		HeapSort(requests);
		
		u64 remove_request_count = requests.count - free_page_indices.count;
		requests.count -= remove_request_count; // We know we won't be able to fulfill these requests this frame.
		
		// Make sure we account for any in flight page outs, we can expect them to be ready soon.
		page_out_count = remove_request_count - Math::Min(in_flight_page_out_count, remove_request_count);
	}
	DebugAssert(free_page_indices.count >= requests.count, "Overflowing page requests didn't get correctly removed. (%/%).", free_page_indices.count, requests.count);
	
	
	// Try to page out pages that are no longer used.
	if (page_out_count != 0) {
		TempAllocationScope(alloc);
		
		Array<MeshletPageOutCandidate> page_out_candidates;
		ArrayReserve(page_out_candidates, alloc, runtime_pages.count);
		
		for (u32 runtime_page_index = 0; runtime_page_index < runtime_pages.count; runtime_page_index += 1) {
			auto& page = runtime_pages[runtime_page_index];
			
			// TODO: Should we try to deallocate currently used pages with lower priority than the newly requested pages?
			if (page.state != MeshletRuntimePageState::Ready || page.cache_frame_index == (u32)current_frame_index) continue;
			
			MeshletPageOutCandidate candidate;
			candidate.asset_page_index   = page.asset_page_index;
			candidate.runtime_page_index = runtime_page_index;
			ArrayAppend(page_out_candidates, candidate);
		}
		
		// Try to remove page_out_count least significant pages.
		HeapSort<MeshletPageOutCandidate>(page_out_candidates, [](const MeshletPageOutCandidate& lh, const MeshletPageOutCandidate& rh)-> bool {
			return lh.asset_page_index > rh.asset_page_index;
		});
		page_out_candidates.count = Math::Min(page_out_candidates.count, page_out_count);
		
		for (u64 i = 0; i < page_out_candidates.count; i += 1) {
			auto& candidate = page_out_candidates[i];
			auto& page = runtime_pages[candidate.runtime_page_index];
			
			page.state      = MeshletRuntimePageState::PageOut;
			page.wait_index = record_context->frame_index + number_of_frames_in_flight;
			
			MeshletRuntimePageUpdateCommand page_table_update_command;
			page_table_update_command.type               = MeshletPageTableUpdateCommandType::PageOut;
			page_table_update_command.runtime_page_index = candidate.runtime_page_index;
			page_table_update_command.mesh_asset_index   = page.mesh_asset_index;
			page_table_update_command.asset_page_index   = page.asset_page_index;
			ArrayAppend(page_table_update_commands, page_table_update_command);
		}
	}
	
	
	auto entity_array = QueryEntityTypeArray<MeshAssetType>(*asset_system);
	auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
	auto& mesh_asset_buffer = GetVirtualResource(record_context, VirtualResourceID::MeshAssetBuffer);
	
	for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
		auto& layout = streams.runtime_data_layout[i];
		auto& allocation = streams.allocation[i];
		
		if (allocation.allocation.index != u32_max) continue;
		
		compile_const u32 page_residency_mask_size = (MeshletPageHeader::max_page_count / 32u) * sizeof(u32);
		u32 file_data_size  = layout.meshlet_group_count * sizeof(MeshletGroup) + page_residency_mask_size;
		u32 allocation_size = file_data_size + layout.page_count * sizeof(u32);
		
		u32 aligned_allocation_size = AlignUp(allocation_size, 4096u);
		
		auto heap_allocation = system->heap.Allocate(aligned_allocation_size);
		if (heap_allocation.index == u32_max) continue;
		
		u32 allocation_offset = (u32)(system->heap.GetMemoryBlockOffset(heap_allocation) + MeshletPageHeader::page_size * MeshletPageHeader::runtime_page_count);
		
		allocation.allocation = heap_allocation;
		allocation.offset     = allocation_offset;
		
		auto& file = streams.runtime_file[i];
		
		u64 file_offset = layout.page_count * MeshletPageHeader::page_size;
		AsyncCopyFileToBuffer(async_transfer_queue, mesh_asset_buffer.buffer.resource, allocation_offset, mesh_asset_buffer.buffer.size, file.file, file_offset, AlignUp(file_data_size, 4096u));
	}
	
	
	// Allocate and read newly requested pages.
	for (u64 subresource_id : requests) {
		u32 mesh_asset_index = (u32)subresource_id;
		u32 asset_page_index = (u32)(subresource_id >> 32);
		u32 runtime_page_index = ArrayPopLast(free_page_indices);
		
		auto& layout = streams.runtime_data_layout[mesh_asset_index];
		auto& file   = streams.runtime_file[mesh_asset_index];
		
		u64 wait_file_index = AsyncCopyFileToBuffer(async_transfer_queue, mesh_asset_buffer.buffer.resource, runtime_page_index * MeshletPageHeader::page_size, mesh_asset_buffer.buffer.size, file.file, asset_page_index * MeshletPageHeader::page_size, MeshletPageHeader::page_size);
		
		auto& page = runtime_pages[runtime_page_index];
		page.state            = MeshletRuntimePageState::FileRead;
		page.mesh_asset_index = mesh_asset_index;
		page.asset_page_index = asset_page_index;
		page.wait_index       = wait_file_index;
	}
	
	system->page_table_update_commands = page_table_update_commands;
}

ArrayView<MeshletRuntimePageUpdateCommand> GetPageTableUpdateCommands(MeshletStreamingSystem* system) {
	auto commands = system->page_table_update_commands;
	system->page_table_update_commands = {};
	
	return commands;
}

void UpdateMeshletStreamingFiles(MeshletStreamingSystem* system, StackAllocator* alloc, AssetEntitySystem* asset_system) {
	extern MeshImportResult ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid);
	
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
				
				auto result = ImportFbxMeshFile(alloc, streams.source_data[i].filepath, layout.file_guid);
				layout = result.layout;
				
				auto& aabb = streams.aabb[i];
				aabb.min = result.aabb_min;
				aabb.max = result.aabb_max;
			}
			
			runtime_file.file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, layout.file_guid), OpenFileFlags::Read | OpenFileFlags::Async);
		}
	}
}
