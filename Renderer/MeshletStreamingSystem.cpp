#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/RecordContext.h"
#include "MeshletStreamingSystem.h"
#include "RenderPasses.h"
#include "StreamingSystem.h"

enum struct MeshletRuntimePageState : u32 {
	Free         = 0,
	FileRead     = 1,
	PageIn       = 2,
	AllocateRTAS = 3,
	BuildRTAS    = 4,
	Ready        = 5,
	PageOut      = 6,
};

struct MeshletRuntimePage {
	u32 mesh_asset_index  = 0;
	u32 asset_page_index  = 0;
	u32 cache_frame_index = 0;
	
	MeshletRuntimePageState state = MeshletRuntimePageState::Free;
	u64 wait_index = 0;
	
	void* readback = nullptr;
	MeshletPageHeader page_header;
	NumaHeapAllocation rtas_allocation;
};

struct MeshletPageOutCandidate {
	u32 asset_page_index   = 0;
	u32 runtime_page_index = 0;
};

struct MeshletStreamingSystem {
	Array<MeshletRuntimePage> runtime_pages;
	Array<u32> free_page_indices;
	
	NumaHeapAllocator rtas_heap;
	Array<u32> rtas_allocation_index_to_page_index;
	
	MeshletStreamingCommands meshlet_streaming_commands;
};

MeshletStreamingSystem* CreateMeshletStreamingSystem(StackAllocator* alloc, u32 meshlet_rtas_buffer_size) {
	auto* system = NewFromAlloc(alloc, MeshletStreamingSystem);
	
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	ArrayResize(system->runtime_pages, alloc, runtime_page_count);
	
	ArrayReserve(system->free_page_indices, alloc, runtime_page_count);
	for (u32 i = 0; i < runtime_page_count; i += 1) {
		ArrayAppend(system->free_page_indices, runtime_page_count - i - 1);
	}
	
	system->rtas_heap = CreateNumaHeapAllocator(alloc, runtime_page_count * 2, meshlet_rtas_buffer_size);
	ArrayResizeMemset(system->rtas_allocation_index_to_page_index, alloc, runtime_page_count * 2, 0xFF);
	
	return system;
}

static u64 EncodeMeshletSubresourceID(u32 mesh_asset_index, u32 asset_page_index) {
	return ((u64)asset_page_index << 32) | mesh_asset_index;
}

static ArrayView<u64> ProcessMeshletStreamingFeedback(RecordContext* record_context, GpuReadbackQueue* meshlet_streaming_feedback_queue, ECS::Component<MeshRuntimeAllocation> allocation_stream) {
	ProfilerScope("ProcessMeshletStreamingFeedback");
	
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
		
		auto& allocation = allocation_stream[mesh_asset_index];
		if (allocation.offset != u32_max) {
			for (u32 i = 0; i < feedback_buffer_size; i += 1) {
				u32 page_mask = meshlet_streaming_feedback_data[read_index++];
				
				for (u32 bit_index : BitScanLow32(page_mask)) {
					u32 asset_page_index = i * 32u + bit_index;
					ArrayAppend(requests, EncodeMeshletSubresourceID(mesh_asset_index, asset_page_index));
				}
			}
		} else {
			read_index += feedback_buffer_size;
		}
	}
	
	return requests;
}

void UpdateMeshletStreamingSystem(MeshletStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* meshlet_streaming_feedback_queue) {
	ProfilerScope("UpdateMeshletStreamingSystem");
	
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	
	auto entity_array = QueryEntityTypeArray<MeshAssetType>(*asset_system);
	auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
	auto* alloc = record_context->alloc;
	
	auto requests = ProcessMeshletStreamingFeedback(record_context, meshlet_streaming_feedback_queue, streams.allocation);
	
	auto& runtime_pages = system->runtime_pages;
	auto& free_page_indices = system->free_page_indices;
	
	Array<MeshletRuntimePageUpdateCommand> page_table_update_commands;
	ArrayReserve(page_table_update_commands, alloc, runtime_page_count);
	
	Array<MeshletRtasBuildCommand> meshlet_rtas_build_commands;
	Array<BuildInputsMeshletRTAS>  meshlet_rtas_build_inputs;
	Array<MoveInputsMeshletRTAS>   meshlet_rtas_move_inputs;
	ArrayReserve(meshlet_rtas_build_commands, alloc, 8u);
	ArrayReserve(meshlet_rtas_build_inputs,   alloc, 8u);
	ArrayReserve(meshlet_rtas_move_inputs,    alloc, 8u);
	
	u32 streaming_scratch_buffer_size = GetBufferSize(record_context, VirtualResourceID::StreamingScratchBuffer);
	
	u32 meshlet_rtas_scratch_offset = 0;
	u32 meshlet_rtas_scratch_size   = streaming_scratch_buffer_size;
	
	u32 page_in_command_count    = 0;
	u64 in_flight_page_out_count = 0;
	
	u64 current_frame_index       = record_context->frame_index;
	u64 completed_file_read_index = CompletedGpuAsyncTransferIndex(async_transfer_queue);
	
	
	// Update page states:
	for (u32 runtime_page_index = 0; runtime_page_index < runtime_pages.count; runtime_page_index += 1) {
		auto& page = runtime_pages[runtime_page_index];
		if (page.state == MeshletRuntimePageState::Free) continue;
		
		// Invalidate all pages that don't have a valid mesh asset allocation (it contains all of the data structures needed to render meshlets).
		if (page.state != MeshletRuntimePageState::PageOut && streams.allocation[page.mesh_asset_index].offset == u32_max) {
			// @InvalidateDuringFileRead:
			// If the page is in FileRead state, we only need to wait for the file read to complete because the page was never used on the GPU.
			// Otherwise we should wait for the current GPU frame to complete (technically waiting for the last GPU frame to complete would be
			// sufficient since it's the last frame that could've accessed the page).
			if (page.state != MeshletRuntimePageState::FileRead) {
				page.wait_index = EncodeGpuFrameWaitIndex(current_frame_index);
			}
			page.state = MeshletRuntimePageState::PageOut;
			
			// @DeallocateRTAS:
			// To prevent memory compaction from moving RTAS after it's invalidated, we deallocate it immediately.
			// RTAS can be deallocated immediately because it's accessed only from the GPU timeline. For example if the same memory is allocated
			// by another page it won't get clobbered by a write from a different timeline (e.g. a file read) while it's still in use.
			if (page.rtas_allocation.index != u32_max) {
				system->rtas_heap.Deallocate(page.rtas_allocation);
				system->rtas_allocation_index_to_page_index[page.rtas_allocation.index] = u32_max;
				page.rtas_allocation = {};
			}
		}
		
		if (page.state == MeshletRuntimePageState::FileRead && IsWaitComplete(page.wait_index, current_frame_index, completed_file_read_index)) {
			page.state      = MeshletRuntimePageState::PageIn;
			page.wait_index = EncodeGpuFrameWaitIndex(current_frame_index);
			
			MeshletRuntimePageUpdateCommand page_table_update_command;
			page_table_update_command.type               = MeshletPageUpdateCommandType::PageIn;
			page_table_update_command.runtime_page_index = runtime_page_index;
			page_table_update_command.mesh_asset_index   = page.mesh_asset_index;
			page_table_update_command.asset_page_index   = page.asset_page_index;
			page_table_update_command.readback_index     = page_in_command_count++;
			ArrayAppend(page_table_update_commands, page_table_update_command);
		}
		
		if (page.state == MeshletRuntimePageState::PageIn && IsWaitComplete(page.wait_index, current_frame_index, completed_file_read_index)) {
			page.page_header = *(MeshletPageHeader*)page.readback;
			page.readback    = nullptr;
			
			page.state      = MeshletRuntimePageState::AllocateRTAS;
			page.wait_index = 0;
		}
		
		if (page.state == MeshletRuntimePageState::AllocateRTAS) {
			MeshletRtasBuildCommand command;
			command.runtime_page_index = runtime_page_index;
			command.mesh_asset_index   = page.mesh_asset_index;
			
			BuildInputsMeshletRTAS inputs;
			inputs.limits.max_meshlet_count        = page.page_header.meshlet_count;
			inputs.limits.max_total_triangle_count = page.page_header.total_triangle_count;
			inputs.limits.max_total_vertex_count   = page.page_header.total_vertex_count;
			auto requirements = GetMeshletRtasMemoryRequirements(record_context->context, inputs.limits);
			
			MoveInputsMeshletRTAS move_inputs;
			move_inputs.limits.max_meshlet_count   = inputs.limits.max_meshlet_count;
			move_inputs.limits.rtas_max_size_bytes = requirements.rtas_max_size_bytes;
			auto move_requirements = GetMeshletRtasMemoryRequirements(record_context->context, move_inputs.limits);
			
			// Move build and move scratch are not used at the same time, could reuse the same memory if needed.
			u32 meshlet_descs_scratch_size      = AlignUp(inputs.limits.max_meshlet_count * 16u * 2u, rtas_alignment);
			u32 indirect_arguments_scratch_size = AlignUp(inputs.limits.max_meshlet_count * 64u, rtas_alignment);
			u32 vertex_buffer_scratch_size      = AlignUp(inputs.limits.max_total_vertex_count * (u32)sizeof(float3), rtas_alignment);
			u32 build_and_move_scratch_size     = AlignUp(Math::Max(requirements.scratch_size_bytes, move_requirements.scratch_size_bytes), rtas_alignment);
			u32 total_scratch_size              = meshlet_descs_scratch_size + indirect_arguments_scratch_size + vertex_buffer_scratch_size + build_and_move_scratch_size;
			
			if (meshlet_rtas_scratch_size >= total_scratch_size) {
				auto allocation = system->rtas_heap.Allocate(AlignUp(requirements.rtas_max_size_bytes, rtas_alignment));
				if (allocation.index != u32_max) {
					page.rtas_allocation = allocation;
					
					system->rtas_allocation_index_to_page_index[allocation.index] = runtime_page_index;
					
					inputs.meshlet_rtas       = GpuAddress(VirtualResourceID::MeshletRtasBuffer,      (u32)system->rtas_heap.GetMemoryBlockOffset(allocation));
					inputs.dst_meshlet_descs  = GpuAddress(VirtualResourceID::StreamingScratchBuffer, meshlet_rtas_scratch_offset);
					inputs.indirect_arguments = GpuAddress(VirtualResourceID::StreamingScratchBuffer, meshlet_rtas_scratch_offset + meshlet_descs_scratch_size);
					inputs.scratch_data       = GpuAddress(VirtualResourceID::StreamingScratchBuffer, meshlet_rtas_scratch_offset + meshlet_descs_scratch_size + indirect_arguments_scratch_size);
					
					move_inputs.src_meshlet_descs = inputs.dst_meshlet_descs;
					move_inputs.dst_meshlet_descs = GpuAddress(VirtualResourceID::StreamingScratchBuffer, meshlet_rtas_scratch_offset + inputs.limits.max_meshlet_count * 16u);
					move_inputs.meshlet_rtas = inputs.meshlet_rtas;
					move_inputs.scratch_data = inputs.scratch_data;
					
					ArrayAppend(meshlet_rtas_build_commands, alloc, command);
					ArrayAppend(meshlet_rtas_build_inputs,   alloc, inputs);
					ArrayAppend(meshlet_rtas_move_inputs,    alloc, move_inputs);
					
					// Vertex buffer is allocated at the end of the scratch space.
					meshlet_rtas_scratch_offset += meshlet_descs_scratch_size + indirect_arguments_scratch_size + build_and_move_scratch_size;
					meshlet_rtas_scratch_size   -= total_scratch_size;
					
					page.state      = MeshletRuntimePageState::BuildRTAS;
					page.wait_index = EncodeGpuFrameWaitIndex(current_frame_index);
				}
			}
		}
		
		if (page.state == MeshletRuntimePageState::BuildRTAS && IsWaitComplete(page.wait_index, current_frame_index, completed_file_read_index)) {
			u32 final_rtas_size = *(u32*)page.readback;
			system->rtas_heap.ReallocateShrink(page.rtas_allocation, final_rtas_size);
			
			page.state      = MeshletRuntimePageState::Ready;
			page.wait_index = 0;
			
			// Don't allow deallocating this page while we have an outstanding page table update command.
			page.cache_frame_index = (u32)current_frame_index;
			
			MeshletRuntimePageUpdateCommand page_table_update_command;
			page_table_update_command.type               = MeshletPageUpdateCommandType::RtasIn;
			page_table_update_command.runtime_page_index = runtime_page_index;
			page_table_update_command.mesh_asset_index   = page.mesh_asset_index;
			page_table_update_command.asset_page_index   = page.asset_page_index;
			ArrayAppend(page_table_update_commands, page_table_update_command);
		}
		
		if (page.state == MeshletRuntimePageState::PageOut) {
			if (IsWaitComplete(page.wait_index, current_frame_index, completed_file_read_index)) {
				page = {};
				ArrayAppend(free_page_indices, runtime_page_index);
			} else {
				in_flight_page_out_count += 1;
			}
		}
	}
	
	// Allocate page header readback buffer for page in commands.
	if (page_in_command_count != 0) {
		auto [gpu_address, cpu_address] = AllocateTransientReadbackBuffer<MeshletPageHeader, 16u>(record_context, page_in_command_count);
		
		for (auto& command : page_table_update_commands) {
			if (command.type == MeshletPageUpdateCommandType::PageIn) {
				runtime_pages[command.runtime_page_index].readback = cpu_address++;
			}
		}
		
		system->meshlet_streaming_commands.page_header_readback       = gpu_address;
		system->meshlet_streaming_commands.page_header_readback_count = page_in_command_count;
	}
	
	// Allocate final RTAS size readback for RTAS build commands.
	if (meshlet_rtas_build_commands.count != 0) {
		auto [gpu_address, cpu_address] = AllocateTransientReadbackBuffer<u32, 16u>(record_context, (u32)meshlet_rtas_build_commands.count);
		
		for (auto& command : meshlet_rtas_build_commands) {
			runtime_pages[command.runtime_page_index].readback = cpu_address++;
		}
		
		system->meshlet_streaming_commands.rtas_page_size_readback = gpu_address;
	}
	
	
	u64 deallocate_page_count = 0;
	if (requests.count != 0) {
		TempAllocationScope(alloc);
		
		HashTable<u64, u32> allocated_page_subresource_id_to_index;
		HashTableReserve(allocated_page_subresource_id_to_index, alloc, runtime_page_count);
		
		for (u32 runtime_page_index = 0; runtime_page_index < runtime_pages.count; runtime_page_index += 1) {
			auto& page = runtime_pages[runtime_page_index];
			if (page.state == MeshletRuntimePageState::Free) continue;
			
			u64 subresource_id = EncodeMeshletSubresourceID(page.mesh_asset_index, page.asset_page_index);
			HashTableAddOrFind(allocated_page_subresource_id_to_index, subresource_id, runtime_page_index);
		}
		
		HeapSort(requests);
		
		requests.count = Math::Min(requests.count, free_page_indices.capacity);
		
		auto& mesh_asset_buffer = GetVirtualResource(record_context, VirtualResourceID::MeshAssetBuffer);
		for (u64 subresource_id : requests) {
			auto* element = HashTableFind(allocated_page_subresource_id_to_index, subresource_id);
			
			if (element != nullptr) {
				runtime_pages[element->value].cache_frame_index = (u32)current_frame_index;
			} else if (free_page_indices.count != 0) {
				u32 mesh_asset_index = (u32)(subresource_id >> 0);
				u32 asset_page_index = (u32)(subresource_id >> 32);
				u32 runtime_page_index = ArrayPopLast(free_page_indices);
				
				u64 wait_file_index = AsyncCopyFileToBuffer(
					async_transfer_queue,
					mesh_asset_buffer.buffer.resource,
					runtime_page_index * MeshletPageHeader::page_size,
					mesh_asset_buffer.buffer.size,
					streams.runtime_file[mesh_asset_index].file,
					asset_page_index * MeshletPageHeader::page_size,
					MeshletPageHeader::page_size
				);
				
				auto& page = runtime_pages[runtime_page_index];
				page.state            = MeshletRuntimePageState::FileRead;
				page.mesh_asset_index = mesh_asset_index;
				page.asset_page_index = asset_page_index;
				page.wait_index       = EncodeFileReadWaitIndex(wait_file_index);
				page.cache_frame_index = (u32)current_frame_index;
			} else {
				// Failed to allocate a page, ask for one page to be deallocated so this page can be allocated.
				deallocate_page_count += 1;
			}
		}
		
		// Make sure we account for any in flight deallocations, we can expect them to be ready soon.
		deallocate_page_count -= Math::Min(in_flight_page_out_count, deallocate_page_count);
	}
	
	// Try to deallocate pages that are no longer used.
	if (deallocate_page_count != 0) {
		TempAllocationScope(alloc);
		
		Array<MeshletPageOutCandidate> page_out_candidates;
		ArrayReserve(page_out_candidates, alloc, runtime_pages.count);
		
		for (u32 runtime_page_index = 0; runtime_page_index < runtime_pages.count; runtime_page_index += 1) {
			auto& page = runtime_pages[runtime_page_index];
			
			bool can_deallocate_state = (page.state == MeshletRuntimePageState::Ready) || (page.state == MeshletRuntimePageState::AllocateRTAS);
			if ((can_deallocate_state == false) || (page.cache_frame_index == (u32)current_frame_index)) continue;
			
			MeshletPageOutCandidate candidate;
			candidate.asset_page_index   = page.asset_page_index;
			candidate.runtime_page_index = runtime_page_index;
			ArrayAppend(page_out_candidates, candidate);
		}
		
		// Try to remove deallocate_page_count least significant pages.
		HeapSort<MeshletPageOutCandidate>(page_out_candidates, [](const MeshletPageOutCandidate& lh, const MeshletPageOutCandidate& rh)-> bool {
			return lh.asset_page_index > rh.asset_page_index;
		});
		page_out_candidates.count = Math::Min(page_out_candidates.count, deallocate_page_count);
		
		for (u64 i = 0; i < page_out_candidates.count; i += 1) {
			auto& candidate = page_out_candidates[i];
			auto& page = runtime_pages[candidate.runtime_page_index];
			
			page.state      = MeshletRuntimePageState::PageOut;
			page.wait_index = EncodeGpuFrameWaitIndex(current_frame_index);
			
			// See @DeallocateRTAS for reference.
			if (page.rtas_allocation.index != u32_max) {
				system->rtas_heap.Deallocate(page.rtas_allocation);
				system->rtas_allocation_index_to_page_index[page.rtas_allocation.index] = u32_max;
				page.rtas_allocation = {};
			}
			
			MeshletRuntimePageUpdateCommand page_table_update_command;
			page_table_update_command.type               = MeshletPageUpdateCommandType::PageOut;
			page_table_update_command.runtime_page_index = candidate.runtime_page_index;
			page_table_update_command.mesh_asset_index   = page.mesh_asset_index;
			page_table_update_command.asset_page_index   = page.asset_page_index;
			ArrayAppend(page_table_update_commands, page_table_update_command);
		}
	}
	
	
	Array<NumaMemoryMoveCommand> move_commands;
	system->rtas_heap.CompactMemoryBlocks(alloc, move_commands);
	
	Array<MeshletRtasMoveCommand> compaction_move_commands;
	ArrayReserve(compaction_move_commands, alloc, move_commands.count);
	
	MoveInputsMeshletRTAS compaction_move_inputs;
	compaction_move_inputs.limits.result_type = IndirectRtasResultType::Explicit;
	
	for (auto& command : move_commands) {
		u32 runtime_page_index = system->rtas_allocation_index_to_page_index[command.allocation.index];
		
		// States before BuildRTAS don't have a RTAS allocation and shouldn't end up here. Pages in PageOut state are
		// assumed to contain invalid data, so we shouldn't try to move their RTASes. This is enforced by deallocating
		// RTASes when transitioning pages to PageOut state, see @DeallocateRTAS for reference.
		auto& page = runtime_pages[runtime_page_index];
		DebugAssert(page.state == MeshletRuntimePageState::BuildRTAS || page.state == MeshletRuntimePageState::Ready, "Cannot move meshlet RTAS page in state '%'.", (u32)page.state);
		
		compaction_move_inputs.limits.max_meshlet_count   += page.page_header.meshlet_count;
		compaction_move_inputs.limits.rtas_max_size_bytes += command.size;
		
		MeshletRtasMoveCommand move_command;
		move_command.runtime_page_index = runtime_page_index;
		move_command.page_address_shift = command.old_offset - command.new_offset;
		ArrayAppend(compaction_move_commands, move_command);
	}
	
	if (compaction_move_commands.count != 0) {
		auto move_requirements = GetMeshletRtasMemoryRequirements(record_context->context, compaction_move_inputs.limits);
		u32 meshlet_descs_scratch_size = AlignUp(compaction_move_inputs.limits.max_meshlet_count * 8u * 2u, rtas_alignment);
		u32 total_scratch_size = meshlet_descs_scratch_size + AlignUp(move_requirements.scratch_size_bytes, rtas_alignment);
		
		compaction_move_inputs.meshlet_rtas      = GpuAddress(VirtualResourceID::MeshletRtasBuffer,      0u);
		compaction_move_inputs.src_meshlet_descs = GpuAddress(VirtualResourceID::StreamingScratchBuffer, 0u);
		compaction_move_inputs.dst_meshlet_descs = GpuAddress(VirtualResourceID::StreamingScratchBuffer, compaction_move_inputs.limits.max_meshlet_count * 8u);
		compaction_move_inputs.scratch_data      = GpuAddress(VirtualResourceID::StreamingScratchBuffer, meshlet_descs_scratch_size);
		DebugAssert(total_scratch_size <= streaming_scratch_buffer_size, "Compaction moves are too large for the allocated scratch buffer.");
	}
	
	
	system->meshlet_streaming_commands.page_table_update_commands   = page_table_update_commands;
	system->meshlet_streaming_commands.meshlet_rtas_build_commands  = meshlet_rtas_build_commands;
	system->meshlet_streaming_commands.meshlet_rtas_build_inputs    = meshlet_rtas_build_inputs;
	system->meshlet_streaming_commands.meshlet_rtas_move_inputs     = meshlet_rtas_move_inputs;
	system->meshlet_streaming_commands.vertex_buffer_scratch_offset = meshlet_rtas_scratch_offset;
	system->meshlet_streaming_commands.compaction_move_commands     = compaction_move_commands;
	system->meshlet_streaming_commands.compaction_move_inputs       = compaction_move_inputs;
}

MeshletStreamingCommands GetMeshletStreamingCommands(MeshletStreamingSystem* system) {
	return system->meshlet_streaming_commands;
}
