#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApiD3D12.h"
#include "GraphicsApi/RecordContext.h"

extern NativeTextureResource transmittance_lut;
extern NativeTextureResource multiple_scattering_lut;
extern NativeTextureResource sky_panorama_lut;

void TransmittanceLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::TransmittanceLut);
}

void TransmittanceLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.transmittance_lut.Bind(transmittance_lut);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, 16, 4, 1);
}

void MultipleScatteringLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::MultipleScatteringLut);
}

void MultipleScatteringLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.transmittance_lut.Bind(transmittance_lut);
	descriptor_table.multiple_scattering_lut.Bind(multiple_scattering_lut);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, 32, 32, 1);
}

void SkyPanoramaLutRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, AtmosphereShadersID, AtmosphereShaders::SkyPanoramaLut);
}

void SkyPanoramaLutRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.transmittance_lut.Bind(transmittance_lut);
	descriptor_table.multiple_scattering_lut.Bind(multiple_scattering_lut);
	descriptor_table.sky_panorama_lut.Bind(sky_panorama_lut);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdDispatch(record_context, 12, 8, 1);
}
