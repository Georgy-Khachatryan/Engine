#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget render_target;
	} pipeline;
	
	pipeline.render_target.format = TextureFormat::R16G16B16A16_FLOAT;
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DrawTestMeshShadersID);
}

void BasicMeshRenderPass::RecordPass(RecordContext* record_context) {
	CmdSetRenderTargets(record_context, VirtualResourceID::SceneRadiance);
	
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

