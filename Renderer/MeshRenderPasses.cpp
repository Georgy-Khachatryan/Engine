#include "Basic/BasicBitArray.h"
#include "RenderPasses.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "EntitySystem/EntitySystem.h"

void MeshletClearBuffersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::ClearBuffers);
}

void MeshletClearBuffersRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	auto& meshlet_streaming_feedback = GetVirtualResource(record_context, VirtualResourceID::MeshletStreamingFeedback);
	CmdDispatch(record_context, DivideAndRoundUp(meshlet_streaming_feedback.buffer.size, (u32)(MeshletConstants::meshlet_culling_thread_group_size * sizeof(u32))));
}

void MeshletAllocateStreamingFeedbackRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::AllocateStreamingFeedback);
}

void MeshletAllocateStreamingFeedbackRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	auto* mesh_assets = QueryEntities<MeshAssetType>(record_context->alloc, *asset_system)[0];
	if (mesh_assets->capacity != 0) { // TODO: Minimize the dispatch size.
		CmdDispatch(record_context, DivideAndRoundUp(mesh_assets->capacity, MeshletConstants::meshlet_culling_thread_group_size));
	}
}


void MeshEntityCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	main_pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshEntityCulling | MeshletCullingShaders::MainPass);
	disocclusion_pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshEntityCulling | MeshletCullingShaders::DisocclusionPass);
}

void MeshEntityCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pass == MeshletCullingPass::Main ? main_pipeline_id : disocclusion_pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	if (pass == MeshletCullingPass::Main) {
		auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
		if (mesh_entities->capacity != 0) { // TODO: Minimize the dispatch size.
			CmdDispatch(record_context, DivideAndRoundUp(mesh_entities->capacity, MeshletConstants::meshlet_culling_thread_group_size));
		}
	} else {
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::RetestMeshEntityCullingCommands * sizeof(uint4)));
	}
}

void MeshletGroupCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	main_pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletGroupCulling | MeshletCullingShaders::MainPass);
	disocclusion_pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletGroupCulling | MeshletCullingShaders::DisocclusionPass);
}

void MeshletGroupCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pass == MeshletCullingPass::Main ? main_pipeline_id : disocclusion_pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	u32 indirect_arguments_offset = 0;
	if (pass == MeshletCullingPass::Main) {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::MeshletGroupCullingCommands;
	} else {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::DisocclusionMeshletGroupCullingCommands;
		
		CmdSetRootArgument(record_context, root_signature.constants, { MeshletConstants::disocclusion_bin_index });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::RetestMeshletGroupCullingCommands * sizeof(uint4)));
	}
	
	for (u32 i = 0; i < MeshletConstants::meshlet_group_culling_command_bin_count; i += 1) {
		CmdSetRootArgument(record_context, root_signature.constants, { i });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (i + indirect_arguments_offset) * sizeof(uint4)));
	}
}

void MeshletCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	main_pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling | MeshletCullingShaders::MainPass);
	disocclusion_pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling | MeshletCullingShaders::DisocclusionPass);
}

void MeshletCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pass == MeshletCullingPass::Main ? main_pipeline_id : disocclusion_pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	u32 indirect_arguments_offset = 0;
	if (pass == MeshletCullingPass::Main) {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::MeshletCullingCommands;
	} else {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::DisocclusionMeshletCullingCommands;
		
		CmdSetRootArgument(record_context, root_signature.constants, { MeshletConstants::disocclusion_bin_index });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::RetestMeshletCullingCommands * sizeof(uint4)));
	}
	
	for (u32 i = 0; i < MeshletConstants::meshlet_culling_command_bin_count; i += 1) {
		CmdSetRootArgument(record_context, root_signature.constants, { i });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (i + indirect_arguments_offset) * sizeof(uint4)));
	}
	
	if (pass == MeshletCullingPass::Disocclusion) {
		auto& meshlet_streaming_feedback = GetVirtualResource(record_context, VirtualResourceID::MeshletStreamingFeedback);
		auto [readback_gpu_address, readback_cpu_address] = AllocateTransientReadbackBuffer(record_context, meshlet_streaming_feedback.buffer.size);
		
		CmdCopyBufferToBuffer(record_context, VirtualResourceID::MeshletStreamingFeedback, readback_gpu_address, meshlet_streaming_feedback.buffer.size);
		meshlet_streaming_feedback_queue->Store(readback_cpu_address, record_context->frame_index);
	}
}

void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget scene_radiance;
		PipelineRenderTarget motion_vectors;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer rasterizer;
	} pipeline;
	
	pipeline.scene_radiance.format  = TextureFormat::R16G16B16A16_FLOAT;
	pipeline.motion_vectors.format = TextureFormat::R16G16_FLOAT;
	pipeline.depth_stencil.flags   = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format  = TextureFormat::D32_FLOAT;
	pipeline.rasterizer.cull_mode  = PipelineRasterizer::CullMode::Back;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DrawTestMeshShadersID, 0, ShaderTypeMask::MeshShader | ShaderTypeMask::PixelShader);
}

void BasicMeshRenderPass::RecordPass(RecordContext* record_context) {
	if (pass == MeshletCullingPass::Main) {
		CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
		CmdClearRenderTarget(record_context, VirtualResourceID::MotionVectors);
	}
	
	FixedCountArray<VirtualResourceID, 2> render_targets;
	render_targets[0] = VirtualResourceID::SceneRadiance;
	render_targets[1] = VirtualResourceID::MotionVectors;
	CmdSetRenderTargets(record_context, render_targets, VirtualResourceID::DepthStencil);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	u32 indirect_arguments_offset = 0;
	if (pass == MeshletCullingPass::Main) {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::DispatchMesh;
	} else {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::DisocclusionDispatchMesh;
	}
	
	CmdDispatchMeshIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, indirect_arguments_offset * sizeof(uint4)));
}

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

struct MeshletStreamingPageTableUpdateCommand {
	u64 subresource_id = 0;
	MeshletPageTableUpdateCommandType type = MeshletPageTableUpdateCommandType::PageIn;
	u32 runtime_page_index = 0;
};

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

void UpdateMeshletPageTableRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, UpdateMeshletPageTableShadersID);
}

void UpdateMeshletPageTableRenderPass::RecordPass(RecordContext* record_context) {
	compile_const u32 runtime_page_count = MeshletPageHeader::runtime_page_count;
	
	auto requests = ProcessMeshletStreamingFeedback(record_context, meshlet_streaming_feedback_queue);
	
	auto* alloc = record_context->alloc;
	
	auto& allocated_pages = meshlet_streaming_system->allocated_pages;
	auto& free_pages = meshlet_streaming_system->free_pages;
	auto& page_out_commands  = meshlet_streaming_system->page_out_commands;
	auto& file_read_commands = meshlet_streaming_system->file_read_commands;
	auto& page_in_commands   = meshlet_streaming_system->page_in_commands;
	u32 cache_inv_frame_index = (u32)(~record_context->frame_index);
	
	
	Array<MeshletStreamingPageTableUpdateCommand> page_table_update_commands;
	ArrayReserve(page_table_update_commands, alloc, runtime_page_count);
	
	if (allocated_pages.data == nullptr) {
		ArrayReserve(allocated_pages, &world_system->heap, runtime_page_count);
		ArrayReserve(free_pages, &world_system->heap, runtime_page_count);
		ArrayReserve(page_out_commands, &world_system->heap, runtime_page_count);
		ArrayReserve(file_read_commands, &world_system->heap, runtime_page_count);
		ArrayReserve(page_in_commands, &world_system->heap, runtime_page_count);
		
		for (u32 i = 0; i < runtime_page_count; i += 1) {
			ArrayAppend(free_pages, runtime_page_count - i - 1);
		}
	}
	
	
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
	
	if (requests.count != 0) {
		auto entity_array = QueryEntityTypeArray<MeshAssetType>(*asset_system);
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		auto& mesh_asset_buffer = GetVirtualResource(record_context, VirtualResourceID::MeshAssetBuffer);
		
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
	}
	
	if (page_table_update_commands.count != 0) {
		using Command = MeshletStreamingPageTableUpdateCommand;
		HeapSort<Command>(page_table_update_commands, [](const Command& lh, const Command& rh)-> bool {
			u64 lh_id = (lh.subresource_id << 32) | (lh.subresource_id >> 32);
			u64 rh_id = (rh.subresource_id << 32) | (rh.subresource_id >> 32);
			
			return lh_id < rh_id;
		});
		
		Array<u32> mesh_asset_page_count;
		ArrayReserve(mesh_asset_page_count, alloc, page_table_update_commands.count);
		
		u32 mesh_asset_count = 0;
		u32 last_mesh_asset_index = u32_max;
		for (auto& command : page_table_update_commands) {
			if (last_mesh_asset_index != (u32)command.subresource_id) {
				last_mesh_asset_index = (u32)command.subresource_id;
				mesh_asset_count += 1;
				ArrayAppend(mesh_asset_page_count, 0u);
			}
			ArrayLastElement(mesh_asset_page_count) += 1;
		}
		
		auto [page_list_gpu_address, page_list_cpu_address] = AllocateTransientUploadBuffer<u32, 16u>(record_context, (u32)page_table_update_commands.count);
		auto [commands_gpu_address,  commands_cpu_address]  = AllocateTransientUploadBuffer<MeshletPageTableUpdateCommand, 16u>(record_context, mesh_asset_count);
		
		u32 page_list_offset = 0;
		u32 commands_offset  = 0;
		last_mesh_asset_index = u32_max;
		for (auto& command : page_table_update_commands) {
			if (last_mesh_asset_index != (u32)command.subresource_id) {
				last_mesh_asset_index = (u32)command.subresource_id;
				
				MeshletPageTableUpdateCommand command;
				command.mesh_asset_index = last_mesh_asset_index;
				command.page_list_offset = (u16)page_list_offset;
				command.page_list_count  = (u16)mesh_asset_page_count[commands_offset];
				commands_cpu_address[commands_offset++] = command;
			}
			
			u32 asset_page_index   = (u32)(command.subresource_id >> 32);
			u32 runtime_page_index = command.runtime_page_index;
			
			page_list_cpu_address[page_list_offset++] = asset_page_index | (runtime_page_index << 16) | ((u32)command.type << 31);
		}
		
		auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
		descriptor_table.commands.Bind(commands_gpu_address, commands_offset * sizeof(MeshletPageTableUpdateCommand));
		descriptor_table.page_list.Bind(page_list_gpu_address, page_list_offset * sizeof(u32));
		
		CmdSetRootSignature(record_context, root_signature);
		CmdSetPipelineState(record_context, pipeline_id);
		
		CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
		
		CmdDispatch(record_context, 1u, commands_offset);
	}
}
