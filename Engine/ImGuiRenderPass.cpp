#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApiD3D12.h"
#include "GraphicsApi/RecordContext.h"

#include <SDK/imgui/imgui.h>

void ImGuiRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateGraphicsPipeline(lib, ImGuiShadersID);
}

void ImGuiRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	FixedCountArray<VirtualResourceID, 1> render_targets;
	render_targets[0] = VirtualResourceID::CurrentBackBuffer;
	CmdClearRenderTarget(record_context, render_targets[0]);
	CmdSetRenderTargets(record_context, render_targets);
	
	auto back_buffer_texture_size = record_context->resource_table->virtual_resources[(u32)VirtualResourceID::CurrentBackBuffer].texture.size;
	CmdSetViewportAndScissor(record_context, uint2(back_buffer_texture_size.x, back_buffer_texture_size.y));
	
	ImGuiPushConstants constants;
	constants.view_to_clip_coef = float4(0.f, 1.f, 0.f, 1.f);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	CmdDrawInstanced(record_context, 3);
}

