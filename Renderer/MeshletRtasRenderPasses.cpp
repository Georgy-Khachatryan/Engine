#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "MeshletStreamingSystem.h"

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
