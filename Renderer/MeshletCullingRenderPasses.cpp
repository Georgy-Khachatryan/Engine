#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "MeshletStreamingSystem.h"

void MeshletClearBuffersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::ClearBuffers);
}

void MeshletClearBuffersRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	u32 meshlet_streaming_feedback_size = GetBufferSize(record_context, VirtualResourceID::MeshletStreamingFeedback);
	u32 mesh_streaming_feedback_size    = GetBufferSize(record_context, VirtualResourceID::MeshStreamingFeedback);
	u32 texture_streaming_feedback_size = GetBufferSize(record_context, VirtualResourceID::TextureStreamingFeedback);
	
	auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
	
	RootSignature::PushConstants constants;
	constants.meshlet_streaming_feedback_size = meshlet_streaming_feedback_size / sizeof(u32);
	constants.mesh_streaming_feedback_size    = mesh_streaming_feedback_size    / sizeof(u32);
	constants.texture_streaming_feedback_size = texture_streaming_feedback_size / sizeof(u32);
	constants.mesh_instance_capacity          = mesh_entities->capacity;
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	u32 largest_buffer_size = 0;
	largest_buffer_size = Math::Max(largest_buffer_size, constants.meshlet_streaming_feedback_size);
	largest_buffer_size = Math::Max(largest_buffer_size, constants.mesh_streaming_feedback_size);
	largest_buffer_size = Math::Max(largest_buffer_size, constants.texture_streaming_feedback_size);
	largest_buffer_size = Math::Max(largest_buffer_size, constants.mesh_instance_capacity);
	largest_buffer_size = Math::Max(largest_buffer_size, LightCullingConstants::grid_element_count);
	
	CmdDispatch(record_context, DivideAndRoundUp(largest_buffer_size, MeshletConstants::meshlet_culling_thread_group_size));
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
	pipeline_ids[(u32)MeshletCullingPass::Main]         = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshEntityCulling | MeshletCullingShaders::MainPass);
	pipeline_ids[(u32)MeshletCullingPass::Disocclusion] = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshEntityCulling | MeshletCullingShaders::DisocclusionPass);
	pipeline_ids[(u32)MeshletCullingPass::Raytracing]   = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshEntityCulling | MeshletCullingShaders::RaytracingPass);
}

void MeshEntityCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_ids[(u32)pass]);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	if (pass == MeshletCullingPass::Main || pass == MeshletCullingPass::Raytracing) {
		auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
		if (mesh_entities->capacity != 0) { // TODO: Minimize the dispatch size.
			CmdDispatch(record_context, DivideAndRoundUp(mesh_entities->capacity, MeshletConstants::meshlet_culling_thread_group_size));
		}
	} else {
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::RetestMeshEntityCullingCommands * sizeof(uint4)));
	}
}

void MeshletGroupCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_ids[(u32)MeshletCullingPass::Main]         = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletGroupCulling | MeshletCullingShaders::MainPass);
	pipeline_ids[(u32)MeshletCullingPass::Disocclusion] = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletGroupCulling | MeshletCullingShaders::DisocclusionPass);
	pipeline_ids[(u32)MeshletCullingPass::Raytracing]   = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletGroupCulling | MeshletCullingShaders::RaytracingPass);
}

void MeshletGroupCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_ids[(u32)pass]);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	compile_const u32 indirect_arguments_offsets[] = {
		(u32)MeshletCullingIndirectArgumentsLayout::MeshletGroupCullingCommands,
		(u32)MeshletCullingIndirectArgumentsLayout::DisocclusionMeshletGroupCullingCommands,
		(u32)MeshletCullingIndirectArgumentsLayout::RaytracingMeshletGroupCullingCommands,
	};
	
	if (pass == MeshletCullingPass::Disocclusion) {
		CmdSetRootArgument(record_context, root_signature.constants, { MeshletConstants::disocclusion_bin_index });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::RetestMeshletGroupCullingCommands * sizeof(uint4)));
	}
	
	u32 indirect_arguments_offset = indirect_arguments_offsets[(u32)pass];
	for (u32 i = 0; i < MeshletConstants::meshlet_group_culling_command_bin_count; i += 1) {
		CmdSetRootArgument(record_context, root_signature.constants, { i });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (i + indirect_arguments_offset) * sizeof(uint4)));
	}
}

void MeshletCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_ids[(u32)MeshletCullingPass::Main]         = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling | MeshletCullingShaders::MainPass);
	pipeline_ids[(u32)MeshletCullingPass::Disocclusion] = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling | MeshletCullingShaders::DisocclusionPass);
	pipeline_ids[(u32)MeshletCullingPass::Raytracing]   = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling | MeshletCullingShaders::RaytracingPass);
}

void MeshletCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_ids[(u32)pass]);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	compile_const u32 indirect_arguments_offsets[] = {
		(u32)MeshletCullingIndirectArgumentsLayout::MeshletCullingCommands,
		(u32)MeshletCullingIndirectArgumentsLayout::DisocclusionMeshletCullingCommands,
		(u32)MeshletCullingIndirectArgumentsLayout::RaytracingMeshletCullingCommands,
	};
	
	if (pass == MeshletCullingPass::Disocclusion) {
		CmdSetRootArgument(record_context, root_signature.constants, { MeshletConstants::disocclusion_bin_index });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::RetestMeshletCullingCommands * sizeof(uint4)));
	}
	
	u32 indirect_arguments_offset = indirect_arguments_offsets[(u32)pass];
	for (u32 i = 0; i < MeshletConstants::meshlet_culling_command_bin_count; i += 1) {
		CmdSetRootArgument(record_context, root_signature.constants, { i });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, (i + indirect_arguments_offset) * sizeof(uint4)));
	}
}

void CopyMeshletCullingStatisticsRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::ReadbackStatistics);
}

void CopyMeshletCullingStatisticsRenderPass::RecordPass(RecordContext* record_context) {
	auto [readback_gpu_address, readback_cpu_address] = AllocateTransientReadbackBuffer<u8, 16u>(record_context, sizeof(MeshletCullingStatistics));
	readback_queue->Store(readback_cpu_address, record_context->frame_index);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.meshlet_culling_statistics.Bind(readback_gpu_address, sizeof(MeshletCullingStatistics));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context);
}

void CopyStreamingFeedbackRenderPass::RecordPass(RecordContext* record_context) {
	struct StreamingFeedbackBuffer {
		VirtualResourceID resource_id    = VirtualResourceID::None;
		GpuReadbackQueue* readback_queue = nullptr;
	};
	
	StreamingFeedbackBuffer feedback_buffers[] = {
		{ VirtualResourceID::MeshletStreamingFeedback, meshlet_streaming_feedback_queue },
		{ VirtualResourceID::MeshStreamingFeedback,    mesh_streaming_feedback_queue    },
		{ VirtualResourceID::TextureStreamingFeedback, texture_streaming_feedback_queue },
	};
	
	for (auto& [resource_id, readback_queue] : feedback_buffers) {
		u32 readback_buffer_size = GetBufferSize(record_context, resource_id);
		auto [readback_gpu_address, readback_cpu_address] = AllocateTransientReadbackBuffer(record_context, readback_buffer_size);
		
		CmdCopyBufferToBuffer(record_context, resource_id, readback_gpu_address, readback_buffer_size);
		readback_queue->Store(readback_cpu_address, record_context->frame_index);
	}
}

void UpdateMeshletPageTableRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, UpdateMeshletPageTableShadersID);
}

void UpdateMeshletPageTableRenderPass::RecordPass(RecordContext* record_context) {
	auto commands = GetMeshletStreamingCommands(meshlet_streaming_system);
	if (commands.page_table_update_commands.count == 0) return;
	
	HeapSort(commands.page_table_update_commands, [](const MeshletRuntimePageUpdateCommand& lh, const MeshletRuntimePageUpdateCommand& rh)-> bool {
		u64 lh_id = ((u64)lh.mesh_asset_index << 32) | (u64)lh.asset_page_index;
		u64 rh_id = ((u64)rh.mesh_asset_index << 32) | (u64)rh.asset_page_index;
		
		return lh_id < rh_id;
	});
	
	Array<u32> mesh_asset_page_count;
	ArrayReserve(mesh_asset_page_count, record_context->alloc, commands.page_table_update_commands.count);
	
	u32 mesh_asset_count = 0;
	u32 last_mesh_asset_index = u32_max;
	for (auto& command : commands.page_table_update_commands) {
		if (last_mesh_asset_index != command.mesh_asset_index) {
			last_mesh_asset_index = command.mesh_asset_index;
			mesh_asset_count += 1;
			ArrayAppend(mesh_asset_page_count, 0u);
		}
		ArrayLastElement(mesh_asset_page_count) += 1;
	}
	
	auto page_table_commands = AllocateTransientUploadBuffer<MeshletPageTableUpdateCommand, 16u>(record_context, mesh_asset_count);
	auto page_commands       = AllocateTransientUploadBuffer<MeshletPageUpdateCommand,      16u>(record_context, (u32)commands.page_table_update_commands.count);
	
	u32 page_command_offset        = 0;
	u32 page_table_commands_offset = 0;
	last_mesh_asset_index = u32_max;
	for (auto& command : commands.page_table_update_commands) {
		if (last_mesh_asset_index != command.mesh_asset_index) {
			last_mesh_asset_index = command.mesh_asset_index;
			
			MeshletPageTableUpdateCommand page_table_command;
			page_table_command.mesh_asset_index    = last_mesh_asset_index;
			page_table_command.page_command_offset = (u16)page_command_offset;
			page_table_command.page_command_count  = (u16)mesh_asset_page_count[page_table_commands_offset];
			page_table_commands.cpu_address[page_table_commands_offset++] = page_table_command;
		}
		
		MeshletPageUpdateCommand page_command;
		page_command.type               = command.type;
		page_command.readback_index     = command.readback_index;
		page_command.asset_page_index   = command.asset_page_index;
		page_command.runtime_page_index = command.runtime_page_index;
		page_commands.cpu_address[page_command_offset++] = page_command;
	}
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.page_table_commands.Bind(page_table_commands.gpu_address, page_table_commands_offset * sizeof(MeshletPageTableUpdateCommand));
	descriptor_table.page_commands.Bind(page_commands.gpu_address, page_command_offset * sizeof(MeshletPageUpdateCommand));
	descriptor_table.page_header_readback.Bind(commands.page_header_readback, commands.page_header_readback_count * sizeof(MeshletPageHeader));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, page_table_commands_offset);
}
