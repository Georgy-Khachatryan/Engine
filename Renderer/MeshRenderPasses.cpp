#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "EntitySystem/EntitySystem.h"

void MeshletClearBuffersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::ClearBuffers);
}

void MeshletClearBuffersRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context);
}


void MeshletCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling);
}

void MeshletCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
	if (mesh_entities->capacity != 0) { // TODO: Use the minimize the dispatch size.
		CmdDispatch(record_context, 1u, mesh_entities->capacity);
	}
}


void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget scene_radiance;
		PipelineRenderTarget motion_vectors;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer rasterizer;
	} pipeline;
	
	pipeline.scene_radiance.format  = TextureFormat::R16G16B16A16_FLOAT;
	pipeline.motion_vectors.format = TextureFormat::R16G16_FLOAT;
	pipeline.depth_stencil.flags   = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format  = TextureFormat::D32_FLOAT;
	pipeline.rasterizer.cull_mode  = PipelineRasterizer::CullMode::Back;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DrawTestMeshShadersID, 0, ShaderTypeMask::MeshShader | ShaderTypeMask::PixelShader);
}

void BasicMeshRenderPass::RecordPass(RecordContext* record_context) {
	CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
	CmdClearRenderTarget(record_context, VirtualResourceID::MotionVectors);
	
	FixedCountArray<VirtualResourceID, 2> render_targets;
	render_targets[0] = VirtualResourceID::SceneRadiance;
	render_targets[1] = VirtualResourceID::MotionVectors;
	CmdSetRenderTargets(record_context, render_targets, VirtualResourceID::DepthStencil);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	CmdDispatchMeshIndirect(record_context, VirtualResourceID::MeshletIndirectArguments);
}

