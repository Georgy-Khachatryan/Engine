#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"


void VisibilityBufferLaydownRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget visibility_buffer;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer   rasterizer;
	} pipeline;
	
	pipeline.visibility_buffer.format = TextureFormat::R32_UINT;
	pipeline.depth_stencil.flags      = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format     = TextureFormat::D32_FLOAT;
	pipeline.rasterizer.cull_mode     = PipelineRasterizer::CullMode::Back;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, VisibilityBufferLaydownShadersID, 0, ShaderTypeMask::MeshShader | ShaderTypeMask::PixelShader);
}

void VisibilityBufferLaydownRenderPass::RecordPass(RecordContext* record_context) {
	if (pass == MeshletCullingPass::Main) {
		CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
		CmdClearRenderTarget(record_context, VirtualResourceID::VisibilityBuffer);
	}
	
	CmdSetRenderTargets(record_context, VirtualResourceID::VisibilityBuffer, VirtualResourceID::DepthStencil);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetRootArgument(record_context, root_signature.constants, { pass });
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::VisibilityBuffer);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	u32 indirect_arguments_offset = 0;
	if (pass == MeshletCullingPass::Main) {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::DispatchMesh;
	} else {
		indirect_arguments_offset = (u32)MeshletCullingIndirectArgumentsLayout::DisocclusionDispatchMesh;
	}
	
	CmdDispatchMeshIndirect(record_context, GpuAddress(VirtualResourceID::MeshletIndirectArguments, indirect_arguments_offset * sizeof(uint4)));
}

void VisibilityBufferResolveRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, VisibilityBufferResolveShadersID);
}

void VisibilityBufferResolveRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::VisibilityBuffer);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}


void DeferredLightingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, DeferredLightingShadersID);
}

void DeferredLightingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetRootArgument(record_context, root_signature.atmosphere, atmosphere);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}


void LightingTemporalDenoiserRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, LightingDenoiserShadersID, LightingDenoiserShaders::TemporalPass);
}

void LightingTemporalDenoiserRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}

void LightingSpatialDenoiserRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, LightingDenoiserShadersID, LightingDenoiserShaders::SpatialPass);
}

void LightingSpatialDenoiserRenderPass::RecordPass(RecordContext* record_context) {
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	for (u32 pass_index = 0; pass_index < 2; pass_index += 1) {
		auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
		
		if (pass_index == 0) {
			descriptor_table.denoiser_radiance_not_blurred_s = VirtualResourceID::DenoiserRadianceHistoryS1;
			descriptor_table.denoiser_radiance_not_blurred_d = VirtualResourceID::DenoiserRadianceHistoryD1;
			descriptor_table.denoiser_radiance_history_s_1   = VirtualResourceID::DenoiserRadianceHistoryS1;
			descriptor_table.denoiser_radiance_history_d_1   = VirtualResourceID::DenoiserRadianceHistoryD1;
			descriptor_table.denoiser_radiance_history_s_0   = VirtualResourceID::DenoiserRadianceSourceS;
			descriptor_table.denoiser_radiance_history_d_0   = VirtualResourceID::DenoiserRadianceSourceD;
		} else {
			descriptor_table.denoiser_radiance_not_blurred_s = VirtualResourceID::DenoiserRadianceHistoryS1;
			descriptor_table.denoiser_radiance_not_blurred_d = VirtualResourceID::DenoiserRadianceHistoryD1;
			descriptor_table.denoiser_radiance_history_s_1   = VirtualResourceID::DenoiserRadianceSourceS;
			descriptor_table.denoiser_radiance_history_d_1   = VirtualResourceID::DenoiserRadianceSourceD;
			descriptor_table.denoiser_radiance_history_s_0   = VirtualResourceID::DenoiserRadianceHistoryS0;
			descriptor_table.denoiser_radiance_history_d_0   = VirtualResourceID::DenoiserRadianceHistoryD0;
		}
		
		CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
		CmdSetRootArgument(record_context, root_signature.constants, { pass_index });
		
		auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
		CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
	} 
}
