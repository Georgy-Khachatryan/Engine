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
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	for (u32 mip_index = 0; mip_index < (u32)render_target_size.mips - build_state.min_ready_mip_level - 1; mip_index += 1) {
		descriptor_table.height_field_mips[mip_index].Bind(VirtualResourceID::TerrainHeightField0, mip_index + build_state.min_ready_mip_level + 1);
	}
	descriptor_table.height_field.Bind(VirtualResourceID::TerrainHeightField0, build_state.min_ready_mip_level, 1);
	
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
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& build_state = *layer_stack_entity.build_state;
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRootArgument(record_context, root_signature.constants, { build_state.min_ready_mip_level });
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
};

struct TerrainCommandList {
	Array<TerrainCommand> commands;
	RecordContext* record_context = nullptr;
	StackAllocator* alloc = nullptr;
	
	u64 hash = 0;
	u32 height_field_epoch = 0;
	s32 frequency_band = 0;
};

static void AppendTerrainCommand(TerrainCommandList& command_list, TerrainCommand command, u8* gpu_settings = nullptr, u32 gpu_settings_size = 0) {
	command.frequency_band = command_list.frequency_band;
	
	u64 command_hash = ComputeHash((u8*)&command, sizeof(TerrainCommand));
	
	if (gpu_settings_size != 0) {
		command_hash = ComputeHash(gpu_settings, gpu_settings_size, command_hash);
		
		auto [gpu_address, cpu_address] = AllocateTransientUploadBuffer(command_list.record_context, gpu_settings_size);
		memcpy(cpu_address, gpu_settings, gpu_settings_size);
		
		// Not hashed.
		command.gpu_settings_offset = gpu_address.offset;
	}
	
	if (command.bindings != TerrainCommandBindings::None) {
		ArrayAppend(command_list.commands, command_list.alloc, command);
	}
	
	if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
		command_list.height_field_epoch += 1;
	}
	
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
	gpu_settings.distortion_amplitude    = cpu_settings.distortion_amplitude;
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
	gpu_settings.amplitude    = cpu_settings.amplitude * cpu_settings.scale * cpu_settings.amount.NormalizedBandScale(command_list.frequency_band);;
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
	gpu_settings.distortion_amount    = cpu_settings.distortion_amount;
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

static void TranslateCommands(TerrainCommandList& command_list, WorldEntitySystem* world_system, ArrayView<GuidComponent> layer_entity_guids, s32 min_frequency_band, s32 mip_level_count) {
	ArrayReserve(command_list.commands, command_list.alloc, layer_entity_guids.count * 8);
	
	s32 last_mip_index = mip_level_count - 1;
	for (s32 mip_index = last_mip_index; mip_index >= 0; mip_index -= 1) {
		command_list.frequency_band = mip_index + TerrainEditorEqualizer::min_frequency_band;
		
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
		
		if (command_list.frequency_band >= min_frequency_band) {
			for (auto [layer_entity_guid] : layer_entity_guids) {
				auto layer = QueryEntityByGUID<TerrainEditorLayerSettingsQuery>(*world_system, layer_entity_guid);
				
				if (layer.noise_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerNoiseCpuSettings(command_list, *layer.noise_cpu_settings);
				} else if (layer.distortion_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerDistortionCpuSettings(command_list, *layer.distortion_cpu_settings);
				} else if (layer.strata_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerStrataCpuSettings(command_list, *layer.strata_cpu_settings);
				} else if (layer.erosion_cpu_settings != nullptr) {
					TranslateCommandTerrainHeightLayerErosionCpuSettings(command_list, *layer.erosion_cpu_settings);
				}
			}
		}
		
		// Copy over to the TerrainHeightField0 so we can preview it.
		if ((command_list.height_field_epoch & 0x1) == 0) {
			TerrainCommand command;
			command.type     = TerrainEditorCommandType::Copy;
			command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
			AppendTerrainCommand(command_list, command);
		}
	}
}

void TerrainEditorLayersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, TerrainEditorLayersShadersID);
}

void TerrainEditorLayersRenderPass::RecordPass(RecordContext* record_context) {
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(*world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(*world_system);
	auto& layer_entity_guids = layer_stack_entity.hierarchy->children;
	auto& build_state = *layer_stack_entity.build_state;
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField0);
	
	auto* alloc = record_context->alloc;
	
	TerrainCommandList command_list;
	command_list.record_context = record_context;
	command_list.alloc          = record_context->alloc;
	TranslateCommands(command_list, world_system, layer_entity_guids, build_state.min_frequency_band, render_target_size.mips);
	
	
	if (build_state.hash != command_list.hash) {
		build_state.hash = command_list.hash;
		build_state.end_command_index  = 0;
		build_state.height_field_epoch = 0;
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
	build_state.min_ready_mip_level = end_command_index >= command_list.commands.count ? 0 : (command_list.commands[end_command_index].frequency_band - TerrainEditorEqualizer::min_frequency_band);
	
	
	HashTable<u64, Descriptors*> descriptor_table_cache;
	HashTableReserve(descriptor_table_cache, alloc, 128);
	
	u32 height_field_epoch = build_state.height_field_epoch;
	for (auto& command : ArrayViewCreate(command_list.commands, begin_command_index, end_command_index)) {
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
	build_state.height_field_epoch = height_field_epoch;
}

