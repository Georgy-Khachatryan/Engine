#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void ReferencePathTracerRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, ReferencePathTracerShadersID, ReferencePathTracerShaders::ReferencePathTracer);
}

void ReferencePathTracerRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetRootArgument(record_context, root_signature.atmosphere, atmosphere);
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
