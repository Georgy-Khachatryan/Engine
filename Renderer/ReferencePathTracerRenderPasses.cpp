#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void ReferencePathTracerRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateRaytracingPipeline(lib, ReferencePathTracerShadersID, ReferencePathTracerShaders::ReferencePathTracer);
}

void ReferencePathTracerRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatchRays(record_context, uint2(GetTextureSize(record_context, VirtualResourceID::SceneRadiance)));
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
