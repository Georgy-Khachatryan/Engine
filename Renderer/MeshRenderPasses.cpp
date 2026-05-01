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
	
	RootSignature::PushConstants constants;
	constants.pass = pass;
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
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


void RaytracingDebugRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, RaytracingDebugShadersID);
}

void RaytracingDebugRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), uint2(8, 4)));
}


void ReferencePathTracerRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, ReferencePathTracerShadersID, ReferencePathTracerShaders::ReferencePathTracer);
}

void ReferencePathTracerRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), uint2(8, 4)));
}

void EnergyCompensationLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, ReferencePathTracerShadersID, ReferencePathTracerShaders::EnergyCompensationLUT);
}

void EnergyCompensationLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, uint2(GetTextureSize(record_context, VirtualResourceID::GgxSingleScatteringEnergyLUT)));
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

void MeshletRtasDecodeVertexBufferRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::MeshletRtasDecodeVertexBuffer);
}

void MeshletRtasDecodeVertexBufferRenderPass::RecordPass(RecordContext* record_context) {
	auto commands = GetMeshletStreamingCommands(meshlet_streaming_system);
	if (commands.meshlet_rtas_build_commands.count == 0) return;
	
	u32 total_meshlet_count = 0;
	
	auto inputs = AllocateTransientUploadBuffer<MeshletRtasDecodeVertexBufferInputs, 12u>(record_context, (u32)commands.meshlet_rtas_build_commands.count);
	for (u32 i = 0; i < commands.meshlet_rtas_build_commands.count; i += 1) {
		auto& command      = commands.meshlet_rtas_build_commands[i];
		auto& build_inputs = commands.meshlet_rtas_build_inputs[i];
		
		total_meshlet_count += build_inputs.limits.max_meshlet_count;
		
		MeshletRtasDecodeVertexBufferInputs shader_inputs;
		shader_inputs.runtime_page_index    = command.runtime_page_index;
		shader_inputs.mesh_asset_index      = command.mesh_asset_index;
		shader_inputs.vertex_buffer_offsets = build_inputs.dst_meshlet_descs.offset;
		inputs.cpu_address[i] = shader_inputs;
	}
	
	auto packed_group_indices = AllocateTransientUploadBuffer<u32, 4u>(record_context, total_meshlet_count);
	for (u32 i = 0, offset = 0; i < commands.meshlet_rtas_build_commands.count; i += 1) {
		auto& build_inputs = commands.meshlet_rtas_build_inputs[i];
		
		for (u32 meshlet_index = 0; meshlet_index < build_inputs.limits.max_meshlet_count; meshlet_index += 1) {
			packed_group_indices.cpu_address[offset++] = i | (meshlet_index << 16);
		}
	}
	
	RootSignature::PushConstants constants;
	constants.vertex_buffer_scratch_offset = commands.vertex_buffer_scratch_offset;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.decode_vertex_buffer_inputs.Bind(inputs.gpu_address, (u32)(commands.meshlet_rtas_build_commands.count * sizeof(MeshletRtasDecodeVertexBufferInputs)));
	descriptor_table.packed_group_indices.Bind(packed_group_indices.gpu_address, total_meshlet_count * sizeof(u32));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	CmdDispatch(record_context, total_meshlet_count);
}

void MeshletRtasBuildRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::MeshletRtasBuildIndirectArguments);
}

void MeshletRtasBuildRenderPass::RecordPass(RecordContext* record_context) {
	auto commands = GetMeshletStreamingCommands(meshlet_streaming_system);
	if (commands.meshlet_rtas_build_commands.count == 0) return;
	
	auto inputs = AllocateTransientUploadBuffer<MeshletRtasBuildIndirectArgumentsInputs, 12u>(record_context, (u32)commands.meshlet_rtas_build_commands.count);
	
	for (u32 i = 0; i < commands.meshlet_rtas_build_commands.count; i += 1) {
		auto& command      = commands.meshlet_rtas_build_commands[i];
		auto& build_inputs = commands.meshlet_rtas_build_inputs[i];
		
		MeshletRtasBuildIndirectArgumentsInputs shader_inputs;
		shader_inputs.runtime_page_index        = command.runtime_page_index;
		shader_inputs.indirect_arguments_offset = build_inputs.indirect_arguments.offset;
		shader_inputs.vertex_buffer_offsets     = build_inputs.dst_meshlet_descs.offset;
		inputs.cpu_address[i] = shader_inputs;
	}
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.meshlet_rtas_inputs.Bind(inputs.gpu_address, (u32)(commands.meshlet_rtas_build_commands.count * sizeof(MeshletRtasBuildIndirectArgumentsInputs)));
	
	RootSignature::PushConstants constants;
	constants.mesh_asset_buffer_address = mesh_asset_buffer_address;
	constants.scratch_buffer_address    = scratch_buffer_address;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, (u32)commands.meshlet_rtas_build_commands.count);
	
	CmdBuildMeshletRTAS(record_context, commands.meshlet_rtas_build_inputs);
	CmdMoveMeshletRTAS(record_context, commands.meshlet_rtas_move_inputs);
}

void MeshletRtasWriteOffsetsRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::MeshletRtasWriteOffsets);
}

void MeshletRtasWriteOffsetsRenderPass::RecordPass(RecordContext* record_context) {
	auto commands = GetMeshletStreamingCommands(meshlet_streaming_system);
	if (commands.meshlet_rtas_build_commands.count == 0) return;
	
	auto inputs = AllocateTransientUploadBuffer<MeshletRtasWriteOffsetsInputs, 8u>(record_context, (u32)commands.meshlet_rtas_build_commands.count);
	
	for (u32 i = 0; i < commands.meshlet_rtas_build_commands.count; i += 1) {
		auto& command     = commands.meshlet_rtas_build_commands[i];
		auto& move_inputs = commands.meshlet_rtas_move_inputs[i];
		
		MeshletRtasWriteOffsetsInputs shader_inputs;
		shader_inputs.runtime_page_index   = command.runtime_page_index;
		shader_inputs.meshlet_descs_offset = move_inputs.dst_meshlet_descs.offset;
		inputs.cpu_address[i] = shader_inputs;
	}
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.write_offsets_inputs.Bind(inputs.gpu_address, (u32)(commands.meshlet_rtas_build_commands.count * sizeof(MeshletRtasWriteOffsetsInputs)));
	descriptor_table.page_size_readback.Bind(commands.rtas_page_size_readback, (u32)(commands.meshlet_rtas_build_commands.count * sizeof(u32)));
	
	RootSignature::PushConstants constants;
	constants.meshlet_rtas_buffer_address = meshlet_rtas_buffer_address;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, (u32)commands.meshlet_rtas_build_commands.count);
}

void MeshletRtasUpdateOffsetsRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::MeshletRtasUpdateOffsets);
}

void MeshletRtasUpdateOffsetsRenderPass::RecordPass(RecordContext* record_context) {
	auto commands = GetMeshletStreamingCommands(meshlet_streaming_system);
	if (commands.compaction_move_commands.count == 0) return;
	
	auto inputs = AllocateTransientUploadBuffer<MeshletRtasUpdateOffsetsInputs, 8u>(record_context, (u32)commands.compaction_move_commands.count);
	
	for (u32 i = 0; i < commands.compaction_move_commands.count; i += 1) {
		auto& command = commands.compaction_move_commands[i];
		
		MeshletRtasUpdateOffsetsInputs shader_inputs;
		shader_inputs.runtime_page_index = command.runtime_page_index;
		shader_inputs.page_address_shift = command.page_address_shift;
		inputs.cpu_address[i] = shader_inputs;
	}
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.update_offsets_inputs.Bind(inputs.gpu_address, (u32)(commands.compaction_move_commands.count * sizeof(MeshletRtasUpdateOffsetsInputs)));
	
	RootSignature::PushConstants constants;
	constants.meshlet_rtas_buffer_address = meshlet_rtas_buffer_address;
	constants.new_addresses_offset = commands.compaction_move_inputs.dst_meshlet_descs.offset;
	constants.old_addresses_offset = commands.compaction_move_inputs.src_meshlet_descs.offset;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, (u32)commands.compaction_move_commands.count);
	
	CmdMoveMeshletRTAS(record_context, { &commands.compaction_move_inputs, 1 });
}


void MeshletBlasBuildIndirectArgumentsRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::MeshletBlasBuildIndirectArguments);
}

void MeshletBlasBuildIndirectArgumentsRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	RootSignature::PushConstants constants;
	constants.scratch_buffer_address = scratch_buffer_address;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
	if (mesh_entities->capacity != 0) { // TODO: Minimize the dispatch size.
		CmdDispatch(record_context, DivideAndRoundUp(mesh_entities->capacity, 256u));
	}
}


void MeshletBlasWriteAddressesRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::MeshletBlasWriteAddresses);
}

void MeshletBlasWriteAddressesRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	RootSignature::PushConstants constants;
	constants.meshlet_rtas_buffer_address = meshlet_rtas_buffer_address;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	u32 indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::RaytracingBuildBLAS;
	CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, indirect_arguments_offset * sizeof(uint4)));
	
	
	BuildInputsMeshletBLAS inputs;
	inputs.limits.max_blas_count          = MeshletConstants::max_meshlet_blas_count;
	inputs.limits.max_total_meshlet_count = MeshletConstants::max_total_blas_meshlets;
	inputs.limits.max_meshlets_per_blas   = MeshletConstants::max_meshlets_per_blas;
	inputs.meshlet_blas            = GpuAddress(VirtualResourceID::MeshletBlasBuffer,      0u);
	inputs.scratch_data            = GpuAddress(VirtualResourceID::StreamingScratchBuffer, MeshletConstants::blas_build_scratch_offset);
	inputs.dst_blas_descs          = GpuAddress(VirtualResourceID::StreamingScratchBuffer, MeshletConstants::blas_build_result_blas_descs_offset);
	inputs.indirect_arguments      = GpuAddress(VirtualResourceID::StreamingScratchBuffer, MeshletConstants::blas_build_indirect_arguments_offset);
	inputs.indirect_argument_count = GpuAddress(VirtualResourceID::MeshletRtasIndirectArguments, (u32)MeshletRtasIndirectArgumentsLayout::CommittedBlasCount * sizeof(u32));
	CmdBuildMeshletBLAS(record_context, inputs);
}


void BuildTlasRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletRtasShadersID, MeshletRtasShaders::BuildMeshEntityInstances);
}

void BuildTlasRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
	if (mesh_entities->capacity != 0) { // TODO: Minimize the dispatch size.
		CmdDispatch(record_context, DivideAndRoundUp(mesh_entities->capacity, 256u));
	}
	
	BuildInputsTLAS inputs;
	inputs.limits.blas_instance_count = mesh_entities->count;
	inputs.result_tlas    = VirtualResourceID::SceneTLAS;
	inputs.instance_descs = VirtualResourceID::TlasMeshInstances;
	inputs.scratch_data   = VirtualResourceID::StreamingScratchBuffer;
	CmdBuildTLAS(record_context, inputs);
}
