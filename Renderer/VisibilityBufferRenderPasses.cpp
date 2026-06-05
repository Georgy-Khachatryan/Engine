#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

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
	CmdSetRootArgument(record_context, root_signature.constants, { pass });
	
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


void VisibilityBufferLaydownRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget visibility_buffer;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer rasterizer;
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


void LightingDenoiserRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, LightingDenoiserShadersID);
}

void LightingDenoiserRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}
