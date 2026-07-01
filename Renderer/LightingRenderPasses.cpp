#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"


void DeferredLightingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, DeferredLightingShadersID, DeferredLightingShaders::DeferredLighting);
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

void BuildVisibleLightTileListRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, DeferredLightingShadersID, DeferredLightingShaders::BuildVisibleLightTileList);
}

void BuildVisibleLightTileListRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), LightingConstants::visible_light_tile_size));
}


void UpdateVisibilityHashTableRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, DeferredLightingShadersID, DeferredLightingShaders::UpdateVisibilityHashTable);
}

void UpdateVisibilityHashTableRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	CmdDispatch(record_context, DivideAndRoundUp(LightingConstants::visibility_hash_table_size, 256u));
}


void IndirectDiffuseRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, IndirectLightingShadersID, IndirectLightingShaders::IndirectDiffuse);
}

void IndirectDiffuseRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	CmdSetRootArgument(record_context, root_signature.atmosphere, atmosphere);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}


void DenoiserDisocclusionMaskRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, LightingDenoiserShadersID, LightingDenoiserShaders::DisocclusionMask);
}

void DenoiserDisocclusionMaskRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
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
			descriptor_table.denoiser_radiance_history_s_1 = VirtualResourceID::DenoiserRadianceHistoryS1;
			descriptor_table.denoiser_radiance_history_d_1 = VirtualResourceID::DenoiserRadianceHistoryD1;
			descriptor_table.denoiser_radiance_history_s_0 = VirtualResourceID::DenoiserRadianceSourceS;
			descriptor_table.denoiser_radiance_history_d_0 = VirtualResourceID::DenoiserRadianceSourceD;
		} else {
			descriptor_table.denoiser_radiance_history_s_1 = VirtualResourceID::DenoiserRadianceSourceS;
			descriptor_table.denoiser_radiance_history_d_1 = VirtualResourceID::DenoiserRadianceSourceD;
			descriptor_table.denoiser_radiance_history_s_0 = VirtualResourceID::DenoiserRadianceHistoryS0;
			descriptor_table.denoiser_radiance_history_d_0 = VirtualResourceID::DenoiserRadianceHistoryD0;
		}
		
		CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
		CmdSetRootArgument(record_context, root_signature.constants, { pass_index });
		
		auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
		CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
	} 
}
