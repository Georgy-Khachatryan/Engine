#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApiD3D12.h"
#include "GraphicsApi/RecordContext.h"

void TransmittanceLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::TransmittanceLut);
}

void TransmittanceLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, DivideAndRoundUp(AtmosphereParameters::transmittance_lut_size, AtmosphereParameters::thread_group_size));
}

void MultipleScatteringLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::MultipleScatteringLut);
}

void MultipleScatteringLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, AtmosphereParameters::multiple_scattering_lut_size);
}

void SkyPanoramaLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::SkyPanoramaLut);
}

void SkyPanoramaLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, DivideAndRoundUp(AtmosphereParameters::sky_panorama_lut_size, AtmosphereParameters::thread_group_size));
}

void AtmosphereCompositeRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::AtmosphereComposite);
}

void AtmosphereCompositeRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, scene_constants);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), AtmosphereParameters::thread_group_size));
}

