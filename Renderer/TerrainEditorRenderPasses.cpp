#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "TerrainEditorEntities.h"

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
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& layer_entity_guids = layer_stack_entity.hierarchy->children;
	if (layer_entity_guids.count == 0) return;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto [layer_entity_guid] = layer_entity_guids[0];
	auto terrain_layer = QueryEntityByGUID<TerrainEditorLayerSettingsQuery>(*world_system, layer_entity_guid);
	
	auto& cpu_settings = *terrain_layer.noise_cpu_settings;
	
	TerrainHeightLayerNoiseGpuSettings gpu_settings;
	gpu_settings.type                    = cpu_settings.type;
	gpu_settings.random_seed             = cpu_settings.random_seed;
	gpu_settings.inv_scale               = 1.f / cpu_settings.scale;
	gpu_settings.anisotropy              = cpu_settings.anisotropy;
	gpu_settings.rotation                = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude               = cpu_settings.amplitude;
	gpu_settings.octave_count            = cpu_settings.octave_count;
	gpu_settings.lacunarity              = cpu_settings.lacunarity;
	gpu_settings.gain                    = cpu_settings.gain;
	gpu_settings.distortion_type         = cpu_settings.distortion_type;
	gpu_settings.ivn_distortion_scale    = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amplitude    = cpu_settings.distortion_amplitude;
	gpu_settings.distortion_octave_count = cpu_settings.distortion_octave_count;
	
	auto layer_constants = AllocateTransientUploadBuffer<TerrainHeightLayerNoiseGpuSettings>(record_context);
	memcpy(layer_constants.cpu_address, &gpu_settings, sizeof(gpu_settings));
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField);
	
	
	RootSignature::PushConstants constants;
	constants.layer_constants_offset = 0;
	constants.render_target_size     = render_target_size.x;
	constants.inv_render_target_size = 1.f / render_target_size.x;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.layer_constants.Bind(layer_constants.gpu_address, sizeof(TerrainHeightLayerNoiseGpuSettings));
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}

