#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

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
	descriptor_table.meshlets.Bind(meshlet_buffer, meshlet_count * sizeof(BasicMeshlet));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, scene_constants);
	
	CmdDispatch(record_context, DivideAndRoundUp(meshlet_count, 256u));
}


void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget render_target;
		PipelineDepthStencil depth_stencil;
	} pipeline;
	
	pipeline.render_target.format = TextureFormat::R16G16B16A16_FLOAT;
	pipeline.depth_stencil.flags  = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format = TextureFormat::D32_FLOAT;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DrawTestMeshShadersID, 0, ShaderTypeMask::MeshShader | ShaderTypeMask::PixelShader);
}

void BasicMeshRenderPass::RecordPass(RecordContext* record_context) {
	CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
	CmdSetRenderTargets(record_context, VirtualResourceID::SceneRadiance, VirtualResourceID::DepthStencil);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.vertices.Bind(vertex_buffer, vertex_count * sizeof(BasicVertex));
	descriptor_table.meshlets.Bind(meshlet_buffer, meshlet_count * sizeof(BasicMeshlet));
	descriptor_table.index_buffer.Bind(index_buffer, AlignUp(index_buffer_size, (u32)sizeof(u32)));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, scene_constants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	CmdDispatchMeshIndirect(record_context, VirtualResourceID::MeshletIndirectArguments);
}

