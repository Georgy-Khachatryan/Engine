#include "Basic/BasicBitArray.h"
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
	
	auto& meshlet_streaming_feedback = GetVirtualResource(record_context, VirtualResourceID::MeshletStreamingFeedback);
	auto& mesh_streaming_feedback    = GetVirtualResource(record_context, VirtualResourceID::MeshStreamingFeedback);
	auto& texture_streaming_feedback = GetVirtualResource(record_context, VirtualResourceID::TextureStreamingFeedback);
	
	RootSignature::PushConstants constants;
	constants.meshlet_streaming_feedback_size = meshlet_streaming_feedback.buffer.size / sizeof(u32);
	constants.mesh_streaming_feedback_size    = mesh_streaming_feedback.buffer.size    / sizeof(u32);
	constants.texture_streaming_feedback_size = texture_streaming_feedback.buffer.size / sizeof(u32);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	u32 largest_buffer_size = Math::Max(Math::Max(constants.meshlet_streaming_feedback_size, constants.mesh_streaming_feedback_size), constants.texture_streaming_feedback_size);
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
		auto& readback_buffer = GetVirtualResource(record_context, resource_id);
		auto [readback_gpu_address, readback_cpu_address] = AllocateTransientReadbackBuffer(record_context, readback_buffer.buffer.size);
		
		CmdCopyBufferToBuffer(record_context, resource_id, readback_gpu_address, readback_buffer.buffer.size);
		readback_queue->Store(readback_cpu_address, record_context->frame_index);
	}
}

void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget scene_radiance;
		PipelineRenderTarget motion_vectors;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer rasterizer;
	} pipeline;
	
	pipeline.scene_radiance.format = TextureFormat::R16G16B16A16_FLOAT;
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

void UpdateMeshletPageTableRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, UpdateMeshletPageTableShadersID);
}

void UpdateMeshletPageTableRenderPass::RecordPass(RecordContext* record_context) {
	auto [page_table_update_commands, page_header_readback, page_header_readback_count] = GetMeshletStreamingUpdateCommands(meshlet_streaming_system);
	if (page_table_update_commands.count == 0) return;
	
	HeapSort(page_table_update_commands, [](const MeshletRuntimePageUpdateCommand& lh, const MeshletRuntimePageUpdateCommand& rh)-> bool {
		u64 lh_id = ((u64)lh.mesh_asset_index << 32) | (u64)lh.asset_page_index;
		u64 rh_id = ((u64)rh.mesh_asset_index << 32) | (u64)rh.asset_page_index;
		
		return lh_id < rh_id;
	});
	
	Array<u32> mesh_asset_page_count;
	ArrayReserve(mesh_asset_page_count, record_context->alloc, page_table_update_commands.count);
	
	u32 mesh_asset_count = 0;
	u32 last_mesh_asset_index = u32_max;
	for (auto& command : page_table_update_commands) {
		if (last_mesh_asset_index != command.mesh_asset_index) {
			last_mesh_asset_index = command.mesh_asset_index;
			mesh_asset_count += 1;
			ArrayAppend(mesh_asset_page_count, 0u);
		}
		ArrayLastElement(mesh_asset_page_count) += 1;
	}
	
	auto page_table_commands = AllocateTransientUploadBuffer<MeshletPageTableUpdateCommand, 16u>(record_context, mesh_asset_count);
	auto page_commands       = AllocateTransientUploadBuffer<MeshletPageUpdateCommand,      16u>(record_context, (u32)page_table_update_commands.count);
	
	u32 page_command_offset        = 0;
	u32 page_table_commands_offset = 0;
	last_mesh_asset_index = u32_max;
	for (auto& command : page_table_update_commands) {
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
	descriptor_table.page_header_readback.Bind(page_header_readback, page_header_readback_count * sizeof(MeshletPageHeader));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, page_table_commands_offset);
}
