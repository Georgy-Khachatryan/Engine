#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "TerrainEditorEntities.h"


void TerrainEditorBuildPreviewRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorPreviewShadersID, TerrainEditorPreviewShaders::BuildPreview);
}

void TerrainEditorBuildPreviewRenderPass::RecordPass(RecordContext* record_context) {
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& build_state = *layer_stack_entity.build_state;
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField0);
	auto thread_group_count = DivideAndRoundUp(uint2(render_target_size) >> (build_state.min_ready_mip_level + 1), 16u);
	
	RootSignature::PushConstants constants;
	constants.render_target_size      = render_target_size.x >> (build_state.min_ready_mip_level + 1);
	constants.inv_render_target_size  = 1.f / constants.render_target_size;
	constants.last_thread_group_index = thread_group_count.x * thread_group_count.y - 1;
	
	auto height_field_resource_id = build_state.epochs.height & 0x1 ? VirtualResourceID::TerrainHeightField0 : VirtualResourceID::TerrainHeightField1;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	for (u32 mip_index = 0; mip_index < (u32)render_target_size.mips - build_state.min_ready_mip_level - 1; mip_index += 1) {
		descriptor_table.height_field_mips[mip_index].Bind(height_field_resource_id, mip_index + build_state.min_ready_mip_level + 1);
	}
	descriptor_table.height_field.Bind(height_field_resource_id, build_state.min_ready_mip_level, 1);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, thread_group_count);
}

void TerrainEditorTracePreviewRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorPreviewShadersID, TerrainEditorPreviewShaders::TracePreview);
}

void TerrainEditorTracePreviewRenderPass::RecordPass(RecordContext* record_context) {
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& build_state = *layer_stack_entity.build_state;
	
	auto height_field_resource_id = build_state.epochs.height & 0x1 ? VirtualResourceID::TerrainHeightField0 : VirtualResourceID::TerrainHeightField1;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.height_field.Bind(height_field_resource_id);
	descriptor_table.preview_mask.Bind(VirtualResourceID::TerrainPreviewMask, Math::Max(build_state.min_ready_mip_level, (u32)(build_state.min_frequency_band - TerrainEditorEqualizer::min_frequency_band)));
	
	RootSignature::PushConstants constants;
	constants.min_mip_level  = build_state.min_ready_mip_level;
	constants.visualize_mask = build_state.visualize_mask ? 1u : 0u;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}

enum struct TerrainCommandBindings : u32 {
	None           = 0,
	SrcHeight      = 1u << 0,
	SrcMask        = 1u << 1,
	SrcFlow        = 1u << 2,
	DstHeight      = 1u << 3,
	DstMask        = 1u << 4,
	DstFlow        = 1u << 5,
	DstFlowX       = 1u << 6,
	DstFlowY       = 1u << 7,
	DstFlowW       = 1u << 8,
	DstErosion     = 1u << 9,
	DstPreviewMask = 1u << 10,
};
ENUM_FLAGS_OPERATORS(TerrainCommandBindings);

struct TerrainCommand {
	TerrainEditorCommandType type = TerrainEditorCommandType::None;
	TerrainCommandBindings bindings = TerrainCommandBindings::None;
	
	s32 frequency_band = 0;
	u32 gpu_settings_offset = 0;
};

struct TerrainCommandList {
	Array<TerrainCommand> commands;
	RecordContext* record_context = nullptr;
	StackAllocator* alloc = nullptr;
	
	u32 render_target_size = 0;
	u64 hash = 0;
	s32 frequency_band = 0;
	TerrainCommandBindings common_bindings = TerrainCommandBindings::None;
	
	TerrainEditorBuildStateEpoch epochs;
};

static void IncrementEpoch(TerrainEditorBuildStateEpoch& epochs, TerrainCommandBindings bindings) {
	if (HasAnyFlags(bindings, TerrainCommandBindings::DstHeight)) {
		epochs.height += 1;
	}
	
	if (HasAnyFlags(bindings, TerrainCommandBindings::DstMask)) {
		epochs.mask += 1;
	}
}

static void AppendTerrainCommand(TerrainCommandList& command_list, TerrainCommand command, u8* gpu_settings = nullptr, u32 gpu_settings_size = 0) {
	if (command.bindings == TerrainCommandBindings::None) return;
	
	command.frequency_band = command_list.frequency_band;
	command.bindings      |= command_list.common_bindings;
	
	u64 command_hash = ComputeHash((u8*)&command, sizeof(TerrainCommand));
	
	if (gpu_settings_size != 0) {
		command_hash = ComputeHash(gpu_settings, gpu_settings_size, command_hash);
		
		auto [gpu_address, cpu_address] = AllocateTransientUploadBuffer(command_list.record_context, gpu_settings_size);
		memcpy(cpu_address, gpu_settings, gpu_settings_size);
		
		// Not hashed.
		command.gpu_settings_offset = gpu_address.offset;
	}
	
	ArrayAppend(command_list.commands, command_list.alloc, command);
	IncrementEpoch(command_list.epochs, command.bindings);
	
	command_list.hash = ComputeHash64(command_list.hash, command_hash);
}

template<typename GpuSettingsT>
static void AppendTerrainCommand(TerrainCommandList& command_list, TerrainCommand& command, GpuSettingsT& gpu_settings) {
	AppendTerrainCommand(command_list, command, (u8*)&gpu_settings, sizeof(GpuSettingsT));
}

static void TranslateCommandTerrainHeightLayerNoiseCpuSettings(TerrainCommandList& command_list, TerrainHeightLayerNoiseCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainHeightLayerNoiseGpuSettings);
	gpu_settings.type                    = cpu_settings.type;
	gpu_settings.random_seed             = cpu_settings.random_seed;
	gpu_settings.inv_scale               = cpu_settings.scale == 0.f ? 1.f : 1.f / cpu_settings.scale;
	gpu_settings.anisotropy              = cpu_settings.anisotropy;
	gpu_settings.rotation                = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude               = cpu_settings.amplitude * cpu_settings.scale * cpu_settings.amount.NormalizedBandScale(command_list.frequency_band);
	gpu_settings.octave_count            = cpu_settings.octave_count;
	gpu_settings.lacunarity              = cpu_settings.lacunarity;
	gpu_settings.gain                    = cpu_settings.gain;
	gpu_settings.distortion_type         = cpu_settings.distortion_type;
	gpu_settings.inv_distortion_scale    = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amplitude    = cpu_settings.distortion_amplitude * cpu_settings.distortion_scale;
	gpu_settings.distortion_octave_count = cpu_settings.distortion_octave_count;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainHeightLayerNoise;
	command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainHeightLayerDistortionCpuSettings(TerrainCommandList& command_list, TerrainHeightLayerDistortionCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainHeightLayerDistortionGpuSettings);
	gpu_settings.type         = cpu_settings.type;
	gpu_settings.random_seed  = cpu_settings.random_seed;
	gpu_settings.inv_scale    = cpu_settings.scale == 0.f ? 1.f : 1.f / cpu_settings.scale;
	gpu_settings.anisotropy   = cpu_settings.anisotropy;
	gpu_settings.rotation     = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude    = cpu_settings.amplitude * cpu_settings.scale * cpu_settings.amount.NormalizedBandScale(command_list.frequency_band);
	gpu_settings.octave_count = cpu_settings.octave_count;
	gpu_settings.lacunarity   = cpu_settings.lacunarity;
	gpu_settings.gain         = cpu_settings.gain;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainHeightLayerDistortion;
	command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainHeightLayerStrataCpuSettings(TerrainCommandList& command_list, TerrainHeightLayerStrataCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainHeightLayerStrataGpuSettings);
	gpu_settings.random_seed          = cpu_settings.random_seed;
	gpu_settings.inv_period           = 1.f / cpu_settings.period;
	gpu_settings.tilt                 = Math::CosSin(cpu_settings.tilt * Math::degrees_to_radians);
	gpu_settings.rotation             = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.randomness           = cpu_settings.randomness;
	gpu_settings.amount               = cpu_settings.amount.NormalizedBandScale(command_list.frequency_band);
	gpu_settings.inv_distortion_scale = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amount    = cpu_settings.distortion_amount; // Not scaled by distortion_scale because the amount is internally scaled by period of the current octave.
	gpu_settings.octave_count         = cpu_settings.octave_count;
	gpu_settings.lacunarity           = cpu_settings.lacunarity;
	gpu_settings.gain                 = cpu_settings.gain;
	
	TerrainCommand command;
	command.type      = TerrainEditorCommandType::TerrainHeightLayerStrata;
	command.bindings  = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainHeightLayerErosionCpuSettings(TerrainCommandList& command_list, TerrainHeightLayerErosionCpuSettings& cpu_settings) {
	using Bindings = TerrainCommandBindings;
	u32 fluvial_iteration_count = (u32)Math::Max((float)cpu_settings.fluvial_iteration_count * cpu_settings.amount.AdditiveBandScale(command_list.frequency_band), 0.f);
	
	if (fluvial_iteration_count != 0) {
		TerrainCommand command;
		command.type      = TerrainEditorCommandType::TerrainHeightLayerErosionClear;
		command.bindings  = Bindings::DstFlow | Bindings::DstFlowX | Bindings::DstFlowY | Bindings::DstFlowW | Bindings::DstErosion;
		AppendTerrainCommand(command_list, command);
	}
	
	for (u32 i = 0; i < fluvial_iteration_count; i += 1) {
		auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainHeightLayerErosionGpuSettings);
		gpu_settings.random_seed               = (u32)ComputeHash64(((u64)cpu_settings.random_seed << 32) | i);
		gpu_settings.iteration_index           = i;
		gpu_settings.iteration_count           = fluvial_iteration_count;
		gpu_settings.fluvial_inertia           = cpu_settings.fluvial_inertia;
		gpu_settings.fluvial_viscosity         = cpu_settings.fluvial_viscosity;
		gpu_settings.fluvial_erosion_rate      = cpu_settings.fluvial_erosion_rate;
		gpu_settings.fluvial_deposition_rate   = cpu_settings.fluvial_deposition_rate;
		gpu_settings.fluvial_evaporation_rate  = cpu_settings.fluvial_evaporation_rate;
		gpu_settings.fluvial_initial_water     = cpu_settings.fluvial_initial_water;
		gpu_settings.thermal_debris_inertia    = cpu_settings.thermal_debris_inertia;
		gpu_settings.thermal_repose_slope      = tanf(cpu_settings.thermal_repose_angle * Math::degrees_to_radians);
		gpu_settings.thermal_sediment_capacity = cpu_settings.thermal_sediment_capacity;
		gpu_settings.thermal_erosion_rate      = cpu_settings.thermal_erosion_rate;
		gpu_settings.thermal_deposition_rate   = cpu_settings.thermal_deposition_rate;
		
		{
			TerrainCommand command;
			command.type      = TerrainEditorCommandType::TerrainHeightLayerErosionSimulate;
			command.bindings  = Bindings::SrcHeight | Bindings::SrcFlow | Bindings::DstFlowX | Bindings::DstFlowY | Bindings::DstFlowW | Bindings::DstErosion;
			AppendTerrainCommand(command_list, command, gpu_settings);
		}
		
		{
			TerrainCommand command;
			command.type      = TerrainEditorCommandType::TerrainHeightLayerErosionApply;
			command.bindings  = Bindings::SrcHeight | Bindings::DstHeight | Bindings::DstFlow | Bindings::DstFlowX | Bindings::DstFlowY | Bindings::DstFlowW | Bindings::DstErosion;
			AppendTerrainCommand(command_list, command, gpu_settings);
		}
	}
}


static void TranslateCommandTerrainMaskLayerNoiseCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerNoiseCpuSettings& cpu_settings, bool is_first_mask_layer) {
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerNoiseGpuSettings);
	gpu_settings.blend_mode              = is_first_mask_layer ? TerrainMaskLayerBlendMode::Override : cpu_settings.blend_mode;
	gpu_settings.type                    = cpu_settings.type;
	gpu_settings.random_seed             = cpu_settings.random_seed;
	gpu_settings.inv_scale               = cpu_settings.scale == 0.f ? 1.f : 1.f / cpu_settings.scale;
	gpu_settings.anisotropy              = cpu_settings.anisotropy;
	gpu_settings.rotation                = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude               = cpu_settings.amplitude;
	gpu_settings.octave_count            = cpu_settings.octave_count;
	gpu_settings.lacunarity              = cpu_settings.lacunarity;
	gpu_settings.gain                    = cpu_settings.gain;
	gpu_settings.distortion_type         = cpu_settings.distortion_type;
	gpu_settings.inv_distortion_scale    = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amplitude    = cpu_settings.distortion_amplitude * cpu_settings.distortion_scale;
	gpu_settings.distortion_octave_count = cpu_settings.distortion_octave_count;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainMaskLayerNoise;
	command.bindings = TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainMaskLayerDistortionCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerDistortionCpuSettings& cpu_settings, bool is_first_mask_layer) {
	if (is_first_mask_layer) return;
	
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerDistortionGpuSettings);
	gpu_settings.blend_mode   = cpu_settings.blend_mode;
	gpu_settings.type         = cpu_settings.type;
	gpu_settings.random_seed  = cpu_settings.random_seed;
	gpu_settings.inv_scale    = cpu_settings.scale == 0.f ? 1.f : 1.f / cpu_settings.scale;
	gpu_settings.anisotropy   = cpu_settings.anisotropy;
	gpu_settings.rotation     = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude    = cpu_settings.amplitude * cpu_settings.scale;
	gpu_settings.octave_count = cpu_settings.octave_count;
	gpu_settings.lacunarity   = cpu_settings.lacunarity;
	gpu_settings.gain         = cpu_settings.gain;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainMaskLayerDistortion;
	command.bindings = TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainMaskLayerSlopeRangeCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerSlopeRangeCpuSettings& cpu_settings, bool is_first_mask_layer) {
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerSlopeRangeGpuSettings);
	gpu_settings.blend_mode = is_first_mask_layer ? TerrainMaskLayerBlendMode::Override : cpu_settings.blend_mode;
	gpu_settings.min_edge.x = cpu_settings.angle * Math::degrees_to_radians;
	
	float falloff_angle_offset = Math::Min(cpu_settings.falloff * Math::degrees_to_radians, Math::HALF_PI - gpu_settings.min_edge.x);
	
	gpu_settings.min_edge.y = gpu_settings.min_edge.x + falloff_angle_offset;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainMaskLayerSlopeRange;
	command.bindings = TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask | TerrainCommandBindings::SrcHeight;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainMaskLayerHeightRangeCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerHeightRangeCpuSettings& cpu_settings, bool is_first_mask_layer) {
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerHeightRangeGpuSettings);
	gpu_settings.blend_mode = is_first_mask_layer ? TerrainMaskLayerBlendMode::Override : cpu_settings.blend_mode;
	gpu_settings.min_edge.x = cpu_settings.height - cpu_settings.range;
	gpu_settings.max_edge.y = cpu_settings.height + cpu_settings.range;
	
	float half_height_delta     = (gpu_settings.max_edge.y - gpu_settings.min_edge.x) * 0.5f;
	float falloff_height_offset = Math::Min(cpu_settings.falloff, half_height_delta);
	
	gpu_settings.min_edge.y = gpu_settings.min_edge.x + falloff_height_offset;
	gpu_settings.max_edge.x = gpu_settings.max_edge.y - falloff_height_offset;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainMaskLayerHeightRange;
	command.bindings = TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask | TerrainCommandBindings::SrcHeight;
	AppendTerrainCommand(command_list, command, gpu_settings);
}

static void TranslateCommandTerrainMaskLayerFlowLinesCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerFlowLinesCpuSettings& cpu_settings, bool is_first_mask_layer) {
	if (is_first_mask_layer && cpu_settings.use_initial_water_mask) return;
	
	compile_const float height_field_extent = 512.f;
	compile_const float water_eps = 1.f / 1024.f;
	compile_const s32   max_steps = 1024;
	
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerFlowLinesGpuSettings);
	gpu_settings.blend_mode               = is_first_mask_layer ? TerrainMaskLayerBlendMode::Override : cpu_settings.blend_mode;
	gpu_settings.random_seed              = cpu_settings.random_seed;
	gpu_settings.use_initial_water_mask   = cpu_settings.use_initial_water_mask ? 1u : 0u;
	gpu_settings.scale                    = cpu_settings.scale;
	gpu_settings.fluvial_inertia          = cpu_settings.fluvial_inertia;
	gpu_settings.fluvial_step_count       = (u32)Math::Clamp((s32)ceilf((float)command_list.render_target_size * (cpu_settings.fluvial_flow_length / height_field_extent)), 0, max_steps);
	gpu_settings.fluvial_evaporation_rate = 1.f - powf(water_eps, 1.f / Math::Max((float)gpu_settings.fluvial_step_count, 1.f));
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::TerrainMaskLayerFlowLinesSimulate;
		command.bindings = TerrainCommandBindings::DstErosion | TerrainCommandBindings::SrcHeight | (cpu_settings.use_initial_water_mask ? TerrainCommandBindings::SrcMask : TerrainCommandBindings::None);
		AppendTerrainCommand(command_list, command, gpu_settings);
	}
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::TerrainMaskLayerFlowLinesApply;
		command.bindings = TerrainCommandBindings::DstErosion | TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask;
		AppendTerrainCommand(command_list, command, gpu_settings);
	}
}

static void TranslateCommandTerrainMaskLayerFlowErosionCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerFlowErosionCpuSettings& cpu_settings, bool is_first_mask_layer) {
	if (is_first_mask_layer) return;
	
	compile_const float height_field_extent = 512.f;
	compile_const float water_eps = 1.f / 1024.f;
	compile_const s32   max_steps = 1024;
	
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerFlowErosionGpuSettings);
	gpu_settings.blend_mode         = cpu_settings.blend_mode;
	gpu_settings.random_seed        = cpu_settings.random_seed;
	gpu_settings.scale              = cpu_settings.scale;
	gpu_settings.fluvial_inertia    = cpu_settings.fluvial_inertia;
	gpu_settings.fluvial_step_count = (u32)Math::Clamp((s32)ceilf((float)command_list.render_target_size * (cpu_settings.fluvial_flow_length / height_field_extent)), 0, max_steps);
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::TerrainMaskLayerFlowErosionSimulate;
		command.bindings = TerrainCommandBindings::DstErosion | TerrainCommandBindings::SrcHeight | TerrainCommandBindings::SrcMask;
		AppendTerrainCommand(command_list, command, gpu_settings);
	}
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::TerrainMaskLayerFlowErosionApply;
		command.bindings = TerrainCommandBindings::DstErosion | TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask;
		AppendTerrainCommand(command_list, command, gpu_settings);
	}
}

static void TranslateCommandTerrainMaskLayerBlurCpuSettings(TerrainCommandList& command_list, TerrainMaskLayerBlurCpuSettings& cpu_settings, bool is_first_mask_layer) {
	if (is_first_mask_layer) return;
	
	compile_const float height_field_extent = 512.f;
	compile_const s32   max_radius = 8;
	
	auto& gpu_settings = *NewFromAlloc(command_list.alloc, TerrainMaskLayerBlurGpuSettings);
	gpu_settings.blend_mode    = cpu_settings.blend_mode;
	gpu_settings.radius_texels = Math::Clamp((s32)ceilf((float)command_list.render_target_size * (cpu_settings.radius / height_field_extent)), 0, max_radius);
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::TerrainMaskLayerBlurVertical;
		command.bindings = TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask;
		AppendTerrainCommand(command_list, command, gpu_settings);
	}
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::TerrainMaskLayerBlurHorizontal;
		command.bindings = TerrainCommandBindings::DstMask | TerrainCommandBindings::SrcMask;
		AppendTerrainCommand(command_list, command, gpu_settings);
	}
}


static void TranslateCommands(TerrainCommandList& command_list, WorldEntitySystem* world_system, TerrainEditorLayerStackEntityType layer_stack_entity, TextureSize render_target_size) {
	auto layer_entity_guids = layer_stack_entity.hierarchy->children;
	auto& build_state = *layer_stack_entity.build_state;
	
	ArrayReserve(command_list.commands, command_list.alloc, layer_entity_guids.count * 8);
	
	s32 last_mip_index = (s32)render_target_size.mips - 1;
	for (s32 mip_index = last_mip_index; mip_index >= 0; mip_index -= 1) {
		command_list.frequency_band     = mip_index + TerrainEditorEqualizer::min_frequency_band;
		command_list.render_target_size = (u32)render_target_size.x >> (u32)mip_index;
		
		if (mip_index == last_mip_index) {
			TerrainCommand command;
			command.type     = TerrainEditorCommandType::Clear;
			command.bindings = TerrainCommandBindings::DstHeight;
			AppendTerrainCommand(command_list, command);
		} else {
			TerrainCommand command;
			command.type     = TerrainEditorCommandType::Upscale;
			command.bindings = TerrainCommandBindings::SrcHeight | TerrainCommandBindings::DstHeight;
			AppendTerrainCommand(command_list, command);
		}
		
		if (command_list.frequency_band >= build_state.min_frequency_band) {
			for (auto [height_layer_entity_guid] : layer_entity_guids) {
				auto height_layer = QueryEntityByGUID<TerrainHeightLayerCpuSettingsQuery>(*world_system, height_layer_entity_guid);
				
				u64 command_cound_before_masks = command_list.commands.count;
				for (auto [mask_layer_entity_guid] : height_layer.hierarchy->children) {
					auto mask_layer = QueryEntityByGUID<TerrainMaskLayerCpuSettingsQuery>(*world_system, mask_layer_entity_guid);
					
					bool is_first_mask_layer = (command_cound_before_masks == command_list.commands.count);
					
					if (mask_layer.noise_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerNoiseCpuSettings(command_list, *mask_layer.noise_cpu_settings, is_first_mask_layer);
					} else if (mask_layer.distortion_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerDistortionCpuSettings(command_list, *mask_layer.distortion_cpu_settings, is_first_mask_layer);
					} else if (mask_layer.slope_range_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerSlopeRangeCpuSettings(command_list, *mask_layer.slope_range_cpu_settings, is_first_mask_layer);
					} else if (mask_layer.height_range_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerHeightRangeCpuSettings(command_list, *mask_layer.height_range_cpu_settings, is_first_mask_layer);
					} else if (mask_layer.flow_lines_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerFlowLinesCpuSettings(command_list, *mask_layer.flow_lines_cpu_settings, is_first_mask_layer);
					} else if (mask_layer.flow_erosion_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerFlowErosionCpuSettings(command_list, *mask_layer.flow_erosion_cpu_settings, is_first_mask_layer);
					} else if (mask_layer.blur_cpu_settings != nullptr) {
						TranslateCommandTerrainMaskLayerBlurCpuSettings(command_list, *mask_layer.blur_cpu_settings, is_first_mask_layer);
					}
					
					if (mask_layer_entity_guid == build_state.visualize_mask_guid) {
						TerrainCommand command;
						command.type     = TerrainEditorCommandType::CopyMask;
						command.bindings = TerrainCommandBindings::SrcMask | TerrainCommandBindings::DstPreviewMask;
						AppendTerrainCommand(command_list, command);
					}
				}
				bool has_mask_layers = (command_cound_before_masks != command_list.commands.count);
				
				command_list.common_bindings = has_mask_layers ? TerrainCommandBindings::SrcMask : TerrainCommandBindings::None;
				if (height_layer.noise_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerNoiseCpuSettings(command_list, *height_layer.noise_cpu_settings);
				} else if (height_layer.distortion_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerDistortionCpuSettings(command_list, *height_layer.distortion_cpu_settings);
				} else if (height_layer.strata_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerStrataCpuSettings(command_list, *height_layer.strata_cpu_settings);
				} else if (height_layer.erosion_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerErosionCpuSettings(command_list, *height_layer.erosion_cpu_settings);
				}
				command_list.common_bindings = TerrainCommandBindings::None;
			}
		}
	}
}

void TerrainEditorLayersRenderPass::InvalidateBuildStates(WorldEntitySystem* world_system) {
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	auto entities = ExtractComponentStreams<TerrainEditorLayerStackEntityType>(layer_stack_entity_array);
	for (u64 i : BitArrayIt(layer_stack_entity_array->alive_mask)) {
		entities.build_state[i].hash = 0;
	}
}

void TerrainEditorLayersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorLayersShadersID);
}

void TerrainEditorLayersRenderPass::RecordPass(RecordContext* record_context) {
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& build_state = *layer_stack_entity.build_state;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField0);
	
	auto* alloc = record_context->alloc;
	
	TerrainCommandList command_list;
	command_list.record_context     = record_context;
	command_list.alloc              = record_context->alloc;
	TranslateCommands(command_list, world_system, layer_stack_entity, render_target_size);
	
	
	if (build_state.hash != command_list.hash) {
		build_state.hash = command_list.hash;
		build_state.end_command_index = 0;
		build_state.epochs = {};
	}
	
	compile_const u64 per_frame_build_pixel_budget = 1024 * 1024 * 128;
	
	u64 begin_command_index = build_state.end_command_index;
	u64 end_command_index   = build_state.end_command_index;
	for (u64 pixel_count = 0; end_command_index < command_list.commands.count && pixel_count < per_frame_build_pixel_budget; end_command_index += 1) {
		auto& command = command_list.commands[end_command_index];
		u32 mip_index = command.frequency_band - TerrainEditorEqualizer::min_frequency_band;
		
		u32 command_render_target_size = (u32)render_target_size.x >> (u32)mip_index;
		
		u32 command_type_multiplier = 1;
		if (command.type == TerrainEditorCommandType::TerrainHeightLayerErosionSimulate) {
			command_type_multiplier = 4;
		}
		
		pixel_count += command_render_target_size * command_render_target_size * command_type_multiplier;
	}
	build_state.end_command_index = end_command_index;
	build_state.min_ready_mip_level = end_command_index >= command_list.commands.count ? 0 : (command_list.commands[end_command_index - 1].frequency_band + 1 - TerrainEditorEqualizer::min_frequency_band);
	
	
	HashTable<u64, Descriptors*> descriptor_table_cache;
	HashTableReserve(descriptor_table_cache, alloc, 128);
	
	auto epochs = build_state.epochs;
	defer{ build_state.epochs = epochs; };
	
	for (auto& command : ArrayViewCreate(command_list.commands, begin_command_index, end_command_index)) {
		u32 mip_index = command.frequency_band - TerrainEditorEqualizer::min_frequency_band;
		
		RootSignature::PushConstants constants;
		constants.command_type           = (u16)command.type;
		constants.has_mask               = HasAnyFlags(command.bindings, TerrainCommandBindings::SrcMask) ? 1 : 0;
		constants.layer_constants_offset = command.gpu_settings_offset;
		constants.render_target_size     = render_target_size.x >> (u32)mip_index;
		constants.inv_render_target_size = 1.f / constants.render_target_size;
		
		
		u64 descriptor_table_key = 0;
		descriptor_table_key |= (u64)command.bindings;
		descriptor_table_key |= (u64)mip_index << 32;
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcHeight | TerrainCommandBindings::DstHeight)) {
			descriptor_table_key |= (u64)(epochs.height & 0x1) << 38;
		}
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcMask | TerrainCommandBindings::DstMask)) {
			descriptor_table_key |= (u64)(epochs.mask & 0x1) << 39;
		}
		
		auto [descriptor_cache_entry, is_added] = HashTableAddOrFind(descriptor_table_cache, alloc, descriptor_table_key, (Descriptors*)nullptr);
		if (is_added) {
			auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
			descriptor_cache_entry->value = &descriptor_table;
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcHeight)) {
				auto resource_id = epochs.height & 0x1 ? VirtualResourceID::TerrainHeightField0 : VirtualResourceID::TerrainHeightField1;
				descriptor_table.height_field_0.Bind(resource_id, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
				auto resource_id = epochs.height & 0x1 ? VirtualResourceID::TerrainHeightField1 : VirtualResourceID::TerrainHeightField0;
				descriptor_table.height_field_1.Bind(resource_id, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcMask)) {
				auto resource_id = epochs.mask & 0x1 ? VirtualResourceID::TerrainMask0 : VirtualResourceID::TerrainMask1;
				descriptor_table.mask_0.Bind(resource_id, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstMask)) {
				auto resource_id = epochs.mask & 0x1 ? VirtualResourceID::TerrainMask1 : VirtualResourceID::TerrainMask0;
				descriptor_table.mask_1.Bind(resource_id, mip_index);
			} else if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstPreviewMask)) {
				descriptor_table.mask_1.Bind(VirtualResourceID::TerrainPreviewMask, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcFlow)) {
				descriptor_table.flow_field_0.Bind(VirtualResourceID::TerrainFlowField, mip_index);
			} else if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstFlow)) {
				descriptor_table.flow_field_1.Bind(VirtualResourceID::TerrainFlowField, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstFlowX)) {
				descriptor_table.flow_field_x_1.Bind(VirtualResourceID::TerrainFlowFieldX, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstFlowY)) {
				descriptor_table.flow_field_y_1.Bind(VirtualResourceID::TerrainFlowFieldY, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstFlowW)) {
				descriptor_table.flow_field_w_1.Bind(VirtualResourceID::TerrainFlowFieldW, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstErosion)) {
				descriptor_table.erosion_field_1.Bind(VirtualResourceID::TerrainErosionField, mip_index);
			}
		}
		
		
		CmdSetRootArgument(record_context, root_signature.descriptor_table, *descriptor_cache_entry->value);
		CmdSetRootArgument(record_context, root_signature.constants, constants);
		
		if (command.type == TerrainEditorCommandType::TerrainHeightLayerErosionSimulate) {
			CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size) >> mip_index, 64u));
		} else {
			CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size) >> mip_index, 16u));
		}
		
		IncrementEpoch(epochs, command.bindings);
	}
}

