#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void TerrainEditorBuildPreviewRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorPreviewShadersID, TerrainEditorPreviewShaders::BuildPreview);
}

void TerrainEditorBuildPreviewRenderPass::RecordPass(RecordContext* record_context) {
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField);
	auto thread_group_count = DivideAndRoundUp(uint2(render_target_size) / 2, 16u);
	
	RootSignature::PushConstants constants;
	constants.render_target_size      = render_target_size.x / 2;
	constants.inv_render_target_size  = 1.f / constants.render_target_size;
	constants.last_thread_group_index = thread_group_count.x * thread_group_count.y - 1;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	for (u32 mip_index = 0; mip_index < (u32)render_target_size.mips - 1; mip_index += 1) {
		descriptor_table.height_field_mips[mip_index] = HLSL::RWTexture2D<float>(VirtualResourceID::TerrainHeightField, mip_index + 1);
	}
	descriptor_table.height_field = HLSL::Texture2D<float>(VirtualResourceID::TerrainHeightField, 0, 1);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	CmdDispatch(record_context, thread_group_count);
}

void TerrainEditorTracePreviewRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorPreviewShadersID, TerrainEditorPreviewShaders::TracePreview);
}

void TerrainEditorTracePreviewRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}


void TerrainEditorLayersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorLayersShadersID);
}

void TerrainEditorLayersRenderPass::RecordPass(RecordContext* record_context) {
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField);
	
	RootSignature::PushConstants constants;
	constants.render_target_size     = render_target_size.x;
	constants.inv_render_target_size = 1.f / render_target_size.x;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}

