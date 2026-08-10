#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"


void CompositeCloudVolumeRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, CloudVolumeShadersID, CloudVolumeShaders::CompositeCloudVolume);
}

void CompositeCloudVolumeRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto* entity_array = QueryEntityTypeArray<CloudVolumeEntityType>(*world_system);
	
	CmdSetRootArgument(record_context, root_signature.constants, { entity_array->capacity });
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	CmdDispatch(record_context, DivideAndRoundUp(CloudConstants::cloud_volume_size, 4u));
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
