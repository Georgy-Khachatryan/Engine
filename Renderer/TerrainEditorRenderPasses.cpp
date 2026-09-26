#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "TerrainEditorEntities.h"


void TerrainEditorBuildPreviewRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorPreviewShadersID, TerrainEditorPreviewShaders::BuildPreview);
}

void TerrainEditorBuildPreviewRenderPass::RecordPass(RecordContext* record_context) {
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField0);
	auto thread_group_count = DivideAndRoundUp(uint2(render_target_size) / 2, 16u);
	
	RootSignature::PushConstants constants;
	constants.render_target_size      = render_target_size.x / 2;
	constants.inv_render_target_size  = 1.f / constants.render_target_size;
	constants.last_thread_group_index = thread_group_count.x * thread_group_count.y - 1;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	for (u32 mip_index = 0; mip_index < (u32)render_target_size.mips - 1; mip_index += 1) {
		descriptor_table.height_field_mips[mip_index].Bind(VirtualResourceID::TerrainHeightField0, mip_index + 1);
	}
	descriptor_table.height_field.Bind(VirtualResourceID::TerrainHeightField0, 0, 1);
	
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

enum struct TerrainCommandBindings : u32 {
	None         = 0,
	SrcHeight    = 1u << 0,
	SrcFlow      = 1u << 1,
	DstHeight    = 1u << 2,
	DstFlow      = 1u << 3, 
	DstFlowX     = 1u << 4, 
	DstFlowY     = 1u << 5, 
	DstFlowW     = 1u << 6, 
	DstErosion   = 1u << 7,
};
ENUM_FLAGS_OPERATORS(TerrainCommandBindings);

struct TerrainCommand {
	TerrainEditorCommandType type = TerrainEditorCommandType::None;
	TerrainCommandBindings bindings = TerrainCommandBindings::None;
	
	s32 frequency_band = 0;
	u32 gpu_settings_offset = 0;
	
	template<typename GpuSettingsT>
	void SetGpuSettings(RecordContext* record_context, GpuSettingsT& gpu_settings_t) {
		auto layer_constants = AllocateTransientUploadBuffer(record_context, sizeof(GpuSettingsT));
		memcpy(layer_constants.cpu_address, &gpu_settings_t, sizeof(GpuSettingsT));
		gpu_settings_offset = layer_constants.gpu_address.offset;
	}
};

static void AppendTerrainCommand(Array<TerrainCommand>& commands, const TerrainCommand& command, RecordContext* record_context, u32& height_field_epoch) {
	if (command.bindings != TerrainCommandBindings::None) {
		ArrayAppend(commands, record_context->alloc, command);
	}
	
	if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
		height_field_epoch += 1;
	}
}

static void TranslateCommandTerrainHeightLayerNoiseCpuSettings(RecordContext* record_context, Array<TerrainCommand>& commands, s32 frequency_band, u32& height_field_epoch, TerrainHeightLayerNoiseCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(record_context->alloc, TerrainHeightLayerNoiseGpuSettings);
	gpu_settings.type                    = cpu_settings.type;
	gpu_settings.random_seed             = cpu_settings.random_seed;
	gpu_settings.inv_scale               = cpu_settings.scale == 0.f ? 1.f : 1.f / cpu_settings.scale;
	gpu_settings.anisotropy              = cpu_settings.anisotropy;
	gpu_settings.rotation                = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude               = cpu_settings.amplitude * cpu_settings.scale * cpu_settings.amount[frequency_band] * cpu_settings.amount.scale;
	gpu_settings.octave_count            = cpu_settings.octave_count;
	gpu_settings.lacunarity              = cpu_settings.lacunarity;
	gpu_settings.gain                    = cpu_settings.gain;
	gpu_settings.distortion_type         = cpu_settings.distortion_type;
	gpu_settings.inv_distortion_scale    = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amplitude    = cpu_settings.distortion_amplitude;
	gpu_settings.distortion_octave_count = cpu_settings.distortion_octave_count;
	
	TerrainCommand command;
	command.type      = TerrainEditorCommandType::TerrainHeightLayerNoise;
	command.bindings  = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	command.frequency_band = frequency_band;
	command.SetGpuSettings(record_context, gpu_settings);
	AppendTerrainCommand(commands, command, record_context, height_field_epoch);
}

static void TranslateCommandTerrainHeightLayerDistortionCpuSettings(RecordContext* record_context, Array<TerrainCommand>& commands, s32 frequency_band, u32& height_field_epoch, TerrainHeightLayerDistortionCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(record_context->alloc, TerrainHeightLayerDistortionGpuSettings);
	gpu_settings.type         = cpu_settings.type;
	gpu_settings.random_seed  = cpu_settings.random_seed;
	gpu_settings.inv_scale    = cpu_settings.scale == 0.f ? 1.f : 1.f / cpu_settings.scale;
	gpu_settings.anisotropy   = cpu_settings.anisotropy;
	gpu_settings.rotation     = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude    = cpu_settings.amplitude * cpu_settings.scale * cpu_settings.amount[frequency_band] * cpu_settings.amount.scale;
	gpu_settings.octave_count = cpu_settings.octave_count;
	gpu_settings.lacunarity   = cpu_settings.lacunarity;
	gpu_settings.gain         = cpu_settings.gain;
	
	TerrainCommand command;
	command.type      = TerrainEditorCommandType::TerrainHeightLayerDistortion;
	command.bindings  = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	command.frequency_band = frequency_band;
	command.SetGpuSettings(record_context, gpu_settings);
	AppendTerrainCommand(commands, command, record_context, height_field_epoch);
}

static void TranslateCommandTerrainHeightLayerStrataCpuSettings(RecordContext* record_context, Array<TerrainCommand>& commands, s32 frequency_band, u32& height_field_epoch, TerrainHeightLayerStrataCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(record_context->alloc, TerrainHeightLayerStrataGpuSettings);
	gpu_settings.random_seed          = cpu_settings.random_seed;
	gpu_settings.inv_period           = 1.f / cpu_settings.period;
	gpu_settings.tilt                 = Math::CosSin(cpu_settings.tilt * Math::degrees_to_radians);
	gpu_settings.rotation             = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.randomness           = cpu_settings.randomness;
	gpu_settings.amount               = cpu_settings.amount[frequency_band] * cpu_settings.amount.scale;
	gpu_settings.inv_distortion_scale = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amount    = cpu_settings.distortion_amount;
	gpu_settings.octave_count         = cpu_settings.octave_count;
	gpu_settings.lacunarity           = cpu_settings.lacunarity;
	gpu_settings.gain                 = cpu_settings.gain;
	
	TerrainCommand command;
	command.type      = TerrainEditorCommandType::TerrainHeightLayerStrata;
	command.bindings  = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	command.frequency_band = frequency_band;
	command.SetGpuSettings(record_context, gpu_settings);
	AppendTerrainCommand(commands, command, record_context, height_field_epoch);
}

static void TranslateCommandTerrainHeightLayerErosionCpuSettings(RecordContext* record_context, Array<TerrainCommand>& commands, s32 frequency_band, u32& height_field_epoch, TerrainHeightLayerErosionCpuSettings& cpu_settings) {
	using Bindings = TerrainCommandBindings;
	u32 fluvial_iteration_count = (u32)Math::Max((float)cpu_settings.fluvial_iteration_count * cpu_settings.amount[frequency_band] * cpu_settings.amount.scale, 0.f);
	
	if (fluvial_iteration_count != 0) {
		TerrainCommand command;
		command.type      = TerrainEditorCommandType::TerrainHeightLayerErosionClear;
		command.bindings  = Bindings::DstFlow | Bindings::DstFlowX | Bindings::DstFlowY | Bindings::DstFlowW | Bindings::DstErosion;
		command.frequency_band = frequency_band;
		AppendTerrainCommand(commands, command, record_context, height_field_epoch);
	}
	
	for (u32 i = 0; i < fluvial_iteration_count; i += 1) {
		auto& gpu_settings = *NewFromAlloc(record_context->alloc, TerrainHeightLayerErosionGpuSettings);
		gpu_settings.random_seed              = (u32)ComputeHash64(((u64)cpu_settings.random_seed << 32) | i);
		gpu_settings.iteration_index          = i;
		gpu_settings.iteration_count          = fluvial_iteration_count;
		gpu_settings.fluvial_inertia          = cpu_settings.fluvial_inertia;
		gpu_settings.fluvial_viscosity        = cpu_settings.fluvial_viscosity;
		gpu_settings.fluvial_erosion_rate     = cpu_settings.fluvial_erosion_rate;
		gpu_settings.fluvial_deposition_rate  = cpu_settings.fluvial_deposition_rate;
		gpu_settings.fluvial_evaporation_rate = cpu_settings.fluvial_evaporation_rate;
		gpu_settings.fluvial_initial_water    = cpu_settings.fluvial_initial_water;
		
		{
			TerrainCommand command;
			command.type      = TerrainEditorCommandType::TerrainHeightLayerErosionSimulate;
			command.bindings  = Bindings::SrcHeight | Bindings::SrcFlow | Bindings::DstFlowX | Bindings::DstFlowY | Bindings::DstFlowW | Bindings::DstErosion;
			command.frequency_band = frequency_band;
			command.SetGpuSettings(record_context, gpu_settings);
			AppendTerrainCommand(commands, command, record_context, height_field_epoch);
		}
		
		{
			TerrainCommand command;
			command.type      = TerrainEditorCommandType::TerrainHeightLayerErosionApply;
			command.bindings  = Bindings::SrcHeight | Bindings::DstHeight | Bindings::DstFlow | Bindings::DstFlowX | Bindings::DstFlowY | Bindings::DstFlowW | Bindings::DstErosion;
			command.frequency_band = frequency_band;
			command.SetGpuSettings(record_context, gpu_settings);
			AppendTerrainCommand(commands, command, record_context, height_field_epoch);
		}
	}
}
	
static ArrayView<TerrainCommand> TranslateCommands(RecordContext* record_context, WorldEntitySystem* world_system, ArrayView<GuidComponent> layer_entity_guids, s32 min_frequency_band, s32 mip_level_count) {
	Array<TerrainCommand> commands;
	ArrayReserve(commands, record_context->alloc, layer_entity_guids.count * 8);
	u32 height_field_epoch = 0;
	
	s32 last_mip_index = mip_level_count - 1;
	for (s32 mip_index = last_mip_index; mip_index >= 0; mip_index -= 1) {
		s32 frequency_band = mip_index + TerrainEditorEqualizer::min_frequency_band;
		
		if (mip_index == last_mip_index) {
			TerrainCommand command;
			command.type      = TerrainEditorCommandType::Clear;
			command.bindings  = TerrainCommandBindings::DstHeight;
			command.frequency_band = frequency_band;
			AppendTerrainCommand(commands, command, record_context, height_field_epoch);
		} else {
			TerrainCommand command;
			command.type      = TerrainEditorCommandType::Upscale;
			command.bindings  = TerrainCommandBindings::SrcHeight | TerrainCommandBindings::DstHeight;
			command.frequency_band = frequency_band;
			AppendTerrainCommand(commands, command, record_context, height_field_epoch);
		}
		
		if (frequency_band >= min_frequency_band) {
			for (auto [layer_entity_guid] : layer_entity_guids) {
				auto layer = QueryEntityByGUID<TerrainEditorLayerSettingsQuery>(*world_system, layer_entity_guid);
				
				if (layer.noise_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerNoiseCpuSettings(record_context, commands, frequency_band, height_field_epoch, *layer.noise_cpu_settings);
				} else if (layer.distortion_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerDistortionCpuSettings(record_context, commands, frequency_band, height_field_epoch, *layer.distortion_cpu_settings);
				} else if (layer.strata_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerStrataCpuSettings(record_context, commands, frequency_band, height_field_epoch, *layer.strata_cpu_settings);
				} else if (layer.erosion_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerErosionCpuSettings(record_context, commands, frequency_band, height_field_epoch, *layer.erosion_cpu_settings);
				}
			}
		}
	}
	
	if ((height_field_epoch & 0x1) == 0) {
		TerrainCommand command;
		command.type      = TerrainEditorCommandType::Copy;
		command.bindings  = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
		command.frequency_band = TerrainEditorEqualizer::min_frequency_band;
		AppendTerrainCommand(commands, command, record_context, height_field_epoch);
	}
	
	return commands;
}

void TerrainEditorLayersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorLayersShadersID);
}

void TerrainEditorLayersRenderPass::RecordPass(RecordContext* record_context) {
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& layer_entity_guids = layer_stack_entity.hierarchy->children;
	auto& preview_state = *layer_stack_entity.preview_state;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField0);
	
	auto* alloc = record_context->alloc;
	
	HashTable<u64, Descriptors*> descriptor_table_cache;
	HashTableReserve(descriptor_table_cache, alloc, 128);
	
	u32 height_field_epoch = 0;
	for (auto& command : TranslateCommands(record_context, world_system, layer_entity_guids, preview_state.min_frequency_band, render_target_size.mips)) {
		u32 mip_index = command.frequency_band - TerrainEditorEqualizer::min_frequency_band;
		
		RootSignature::PushConstants constants;
		constants.command_type           = command.type;
		constants.layer_constants_offset = command.gpu_settings_offset;
		constants.render_target_size     = render_target_size.x >> (u32)mip_index;
		constants.inv_render_target_size = 1.f / constants.render_target_size;
		
		
		u64 descriptor_table_key = 0;
		descriptor_table_key |= (u64)command.bindings;
		descriptor_table_key |= (u64)mip_index << 32;
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcHeight | TerrainCommandBindings::DstHeight)) {
			descriptor_table_key |= (u64)(height_field_epoch & 0x1) << 38;
		}
		
		auto [descriptor_cache_entry, is_added] = HashTableAddOrFind(descriptor_table_cache, alloc, descriptor_table_key, (Descriptors*)nullptr);
		if (is_added) {
			auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
			descriptor_cache_entry->value = &descriptor_table;
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcHeight)) {
				auto resource_id = height_field_epoch & 0x1 ? VirtualResourceID::TerrainHeightField0 : VirtualResourceID::TerrainHeightField1;
				descriptor_table.height_field_0.Bind(resource_id, mip_index);
			}
			
			if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
				auto resource_id = height_field_epoch & 0x1 ? VirtualResourceID::TerrainHeightField1 : VirtualResourceID::TerrainHeightField0;
				descriptor_table.height_field_1.Bind(resource_id, mip_index);
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
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
			height_field_epoch += 1;
		}
	}
}

