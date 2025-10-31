#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget render_target;
		PipelineDepthStencil depth_stencil;
	} pipeline;
	
	pipeline.render_target.format = TextureFormat::R16G16B16A16_FLOAT;
	pipeline.depth_stencil.flags  = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format = TextureFormat::D32_FLOAT;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DrawTestMeshShadersID);
}

void BasicMeshRenderPass::RecordPass(RecordContext* record_context) {
	CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
	CmdSetRenderTargets(record_context, VirtualResourceID::SceneRadiance, VirtualResourceID::DepthStencil);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.vertices.Bind(vertex_buffer, vertex_count * sizeof(BasicVertex));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, scene_constants);
	CmdSetIndexBufferView(record_context, index_buffer, index_count * sizeof(u32), TextureFormat::R32_UINT);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	CmdDrawIndexedInstanced(record_context, index_count);
}

