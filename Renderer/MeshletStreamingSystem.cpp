#include "MeshletStreamingSystem.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "RenderPasses.h"
#include "GraphicsApi/RecordContext.h"
#include "GraphicsApi/AsyncTransferQueue.h"

struct MeshletStreamingPage {
	u64 subresource_id = 0; // (asset_page_index << 32) | mesh_asset_index, lowest IDs are streamed in first.
	u64 cache_priority = 0; // (~frame_index << 32) | asset_page_index, lowest values are retained for the longer.
	u32 runtime_page_index = 0;
};

struct MeshletStreamingPageOutCommand {
	u64 subresource_id = 0;
	u64 wait_frame_index = 0;
	u32 runtime_page_index = 0;
};

struct MeshletStreamingPageFileReadCommand {
	u64 subresource_id  = 0;
	u64 wait_file_index = 0;
	u32 runtime_page_index = 0;
};

struct MeshletStreamingPageInCommand {
	u64 subresource_id = 0;
	u64 wait_frame_index = 0;
	u32 runtime_page_index = 0;
};

struct MeshletStreamingSystem {
	NumaHeapAllocator heap;
	
	Array<MeshletStreamingPage> allocated_pages;
	Array<u32> free_pages;
	Array<MeshletStreamingPageOutCommand> page_out_commands;
	Array<MeshletStreamingPageFileReadCommand> file_read_commands;
	Array<MeshletStreamingPageInCommand> page_in_commands;
	
	ArrayView<MeshletStreamingPageTableUpdateCommand> page_table_update_commands;
};

MeshletStreamingSystem* CreateMeshletStreamingSystem(StackAllocator* alloc, u64 buffer_size) {
	auto* system = NewFromAlloc(alloc, MeshletStreamingSystem);
	
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	ArrayReserve(system->allocated_pages, alloc, runtime_page_count);
	ArrayReserve(system->free_pages, alloc, runtime_page_count);
	ArrayReserve(system->page_out_commands, alloc, runtime_page_count);
	ArrayReserve(system->file_read_commands, alloc, runtime_page_count);
	ArrayReserve(system->page_in_commands, alloc, runtime_page_count);
	
	for (u32 i = 0; i < runtime_page_count; i += 1) {
		ArrayAppend(system->free_pages, runtime_page_count - i - 1);
	}
	
	system->heap = CreateNumaHeapAllocator(alloc, 1024, (u32)buffer_size);
	
	return system;
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
				ArrayAppend(requests, ((u64)asset_page_index << 32) | mesh_asset_index);
			}
		}
	}
	
	return requests;
}

void UpdateMeshletStreamingSystem(MeshletStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* meshlet_streaming_feedback_queue) {
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	auto* alloc = record_context->alloc;
	
	auto requests = ProcessMeshletStreamingFeedback(record_context, meshlet_streaming_feedback_queue);
	
	auto& allocated_pages = system->allocated_pages;
	auto& free_pages = system->free_pages;
	auto& page_out_commands  = system->page_out_commands;
	auto& file_read_commands = system->file_read_commands;
	auto& page_in_commands   = system->page_in_commands;
	u32 cache_inv_frame_index = (u32)(~record_context->frame_index);
	
	
	Array<MeshletStreamingPageTableUpdateCommand> page_table_update_commands;
	ArrayReserve(page_table_update_commands, alloc, runtime_page_count);
	
	{
		u64 current_frame_index = record_context->frame_index;
		u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
		
		for (u32 i = 0; i < page_out_commands.count;) {
			auto& command = page_out_commands[i];
			if (command.wait_frame_index <= current_frame_index) {
				ArrayAppend(free_pages, command.runtime_page_index);
				ArrayEraseSwapLast(page_out_commands, i);
			} else {
				i += 1;
			}
		}
		
		for (u32 i = 0; i < file_read_commands.count;) {
			auto& command = file_read_commands[i];
			if (command.wait_file_index <= completed_file_read_index) {
				MeshletStreamingPageInCommand page_in_command;
				page_in_command.wait_frame_index   = current_frame_index + number_of_frames_in_flight;
				page_in_command.subresource_id     = command.subresource_id;
				page_in_command.runtime_page_index = command.runtime_page_index;
				ArrayAppend(page_in_commands, page_in_command); // TODO: Could be a ring buffer.
				
				MeshletStreamingPageTableUpdateCommand page_table_update_command;
				page_table_update_command.type               = MeshletPageTableUpdateCommandType::PageIn;
				page_table_update_command.runtime_page_index = command.runtime_page_index;
				page_table_update_command.subresource_id     = command.subresource_id;
				ArrayAppend(page_table_update_commands, page_table_update_command);
				
				ArrayEraseSwapLast(file_read_commands, i);
			} else {
				i += 1;
			}
		}
		
		for (u32 i = 0; i < page_in_commands.count;) {
			auto& command = page_in_commands[i];
			if (command.wait_frame_index <= current_frame_index) {
				MeshletStreamingPage page;
				page.subresource_id     = command.subresource_id;
				page.cache_priority     = ((u64)cache_inv_frame_index << 32) | (command.subresource_id >> 32);
				page.runtime_page_index = command.runtime_page_index;
				ArrayAppend(allocated_pages, page);
				
				ArrayEraseSwapLast(page_in_commands, i);
			} else {
				i += 1;
			}
		}
	}
	
	if (requests.count != 0) {
		TempAllocationScope(alloc);
		
		HashTable<u64, u32> allocated_page_subresource_id_to_index;
		HashTableReserve(allocated_page_subresource_id_to_index, alloc, runtime_page_count);
		
		for (u32 i = 0; i < allocated_pages.count; i += 1) {
			HashTableAddOrFind(allocated_page_subresource_id_to_index, allocated_pages[i].subresource_id, i);
		}
		
		for (auto& command : page_out_commands)  HashTableAddOrFind(allocated_page_subresource_id_to_index, command.subresource_id, u32_max);
		for (auto& command : file_read_commands) HashTableAddOrFind(allocated_page_subresource_id_to_index, command.subresource_id, u32_max);
		for (auto& command : page_in_commands)   HashTableAddOrFind(allocated_page_subresource_id_to_index, command.subresource_id, u32_max);
		
		for (u32 i = 0; i < requests.count;) {
			u64 subresource_id = requests[i];
			auto* element = HashTableFind(allocated_page_subresource_id_to_index, subresource_id);
			
			if (element != nullptr) {
				if (element->value != u32_max) {
					allocated_pages[element->value].cache_priority = ((u64)cache_inv_frame_index << 32) | (subresource_id >> 32);
				}
				ArrayEraseSwapLast(requests, i);
			} else {
				i += 1;
			}
		}
	}
	
	// At this point we only have requests that don't already have an allocated page.
	u64 stream_out_count = 0;
	if (requests.count > free_pages.count) {
		HeapSort(requests);
		
		u64 remove_request_count = requests.count - free_pages.count;
		requests.count -= remove_request_count; // We know we won't be able to fulfill these requests this frame.
		
		// Make sure we account for any pending page out commands, we can expect them to be ready soon.
		stream_out_count = remove_request_count - Math::Min(page_out_commands.count, remove_request_count);
	}
	DebugAssert(free_pages.count >= requests.count, "Overflowing page requests didn't get correctly removed. (%/%).", free_pages.count, requests.count);
	
	if (stream_out_count != 0) {
		// Try to remove stream_out_count least significant pages.
		HeapSort<MeshletStreamingPage>(allocated_pages, [](const MeshletStreamingPage& lh, const MeshletStreamingPage& rh)-> bool {
			return lh.cache_priority < rh.cache_priority;
		});
		
		s64 try_deallocate_up_to_index = Math::Max((s64)allocated_pages.count - (s64)stream_out_count, 0ll);
		for (s64 i = allocated_pages.count - 1; i >= try_deallocate_up_to_index; i -= 1) {
			auto& page = allocated_pages[i];
			
			// TODO: Should we try to deallocate currently used pages with lower priority than the newly requested pages?
			if ((page.cache_priority >> 32) == cache_inv_frame_index) break;
			
			MeshletStreamingPageOutCommand page_out_command;
			page_out_command.wait_frame_index   = record_context->frame_index + number_of_frames_in_flight;
			page_out_command.subresource_id     = page.subresource_id;
			page_out_command.runtime_page_index = page.runtime_page_index;
			ArrayAppend(page_out_commands, page_out_command); // TODO: Could be a ring buffer.
			
			MeshletStreamingPageTableUpdateCommand page_table_update_command;
			page_table_update_command.type               = MeshletPageTableUpdateCommandType::PageOut;
			page_table_update_command.runtime_page_index = page.runtime_page_index;
			page_table_update_command.subresource_id     = page.subresource_id;
			ArrayAppend(page_table_update_commands, page_table_update_command);
			
			ArrayPopLast(allocated_pages);
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
	
	for (u64 subresource_id : requests) {
		u32 mesh_asset_index = (u32)subresource_id;
		u32 asset_page_index = (u32)(subresource_id >> 32);
		u32 runtime_page_index = ArrayPopLast(free_pages);
		
		auto& layout = streams.runtime_data_layout[mesh_asset_index];
		auto& file   = streams.runtime_file[mesh_asset_index];
		
		u64 wait_file_index = AsyncCopyFileToBuffer(async_transfer_queue, mesh_asset_buffer.buffer.resource, runtime_page_index * MeshletPageHeader::page_size, mesh_asset_buffer.buffer.size, file.file, asset_page_index * MeshletPageHeader::page_size, MeshletPageHeader::page_size);
		
		MeshletStreamingPageFileReadCommand file_read_command;
		file_read_command.subresource_id     = subresource_id;
		file_read_command.wait_file_index    = wait_file_index;
		file_read_command.runtime_page_index = runtime_page_index;
		ArrayAppend(file_read_commands, file_read_command);
	}
	
	system->page_table_update_commands = page_table_update_commands;
}

ArrayView<MeshletStreamingPageTableUpdateCommand> GetPageTableUpdateCommands(MeshletStreamingSystem* system) {
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
