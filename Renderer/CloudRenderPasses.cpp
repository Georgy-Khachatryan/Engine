#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"


void CloudEntityCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, CloudVolumeShadersID, CloudVolumeShaders::CloudEntityCulling);
}

void CloudEntityCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto* cloud_entities = QueryEntityTypeArray<CloudVolumeEntityType>(*world_system);
	if (cloud_entities->capacity != 0) { // TODO: Minimize the dispatch size.
		CmdDispatch(record_context, DivideAndRoundUp(cloud_entities->capacity, 256u));
	}
}

void CloudCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, CloudVolumeShadersID, CloudVolumeShaders::CloudCulling);
}

void CloudCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	for (u32 i = 0; i < CloudCullingConstants::culling_command_bin_count; i += 1) {
		CmdSetRootArgument(record_context, root_signature.constants, { i });
		CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::CloudCullingIndirectArguments, i * sizeof(uint4)));
	}
}

void BuildCloudUpdateListRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, CloudVolumeShadersID, CloudVolumeShaders::BuildCloudUpdateList);
}

void BuildCloudUpdateListRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	CmdDispatch(record_context, DivideAndRoundUp(CloudConstants::cloud_volume_size / 8u, 4u));
}

void CompositeCloudVolumeRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, CloudVolumeShadersID, CloudVolumeShaders::CompositeCloudVolume);
}

void CompositeCloudVolumeRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	CmdDispatchIndirect(record_context, GpuAddress(VirtualResourceID::CloudCullingIndirectArguments, CloudCullingConstants::culling_command_bin_count * sizeof(uint4)));
}

void BuildCloudVolumeMaskRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, CloudVolumeShadersID, CloudVolumeShaders::BuildCloudVolumeMask);
}

void BuildCloudVolumeMaskRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	CmdDispatch(record_context, DivideAndRoundUp(CloudConstants::cloud_volume_size / 4u, 4u));
}
