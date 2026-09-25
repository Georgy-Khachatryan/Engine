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
	None      = 0,
	SrcHeight = 1u << 0,
	DstHeight = 1u << 1,
};
ENUM_FLAGS_OPERATORS(TerrainCommandBindings);

struct TerrainCommand {
	TerrainEditorCommandType type = TerrainEditorCommandType::None;
	TerrainCommandBindings bindings = TerrainCommandBindings::None;
	
	u32 gpu_settings_size = 0;
	void* gpu_settings = nullptr;
	
	template<typename GpuSettingsT>
	void SetGpuSettings(GpuSettingsT& gpu_settings_t) {
		gpu_settings = &gpu_settings_t;
		gpu_settings_size = sizeof(GpuSettingsT);
	}
};

static TerrainCommand TranslateCommandTerrainHeightLayerNoiseCpuSettings(StackAllocator* alloc, TerrainHeightLayerNoiseCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(alloc, TerrainHeightLayerNoiseGpuSettings);
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
	gpu_settings.inv_distortion_scale    = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amplitude    = cpu_settings.distortion_amplitude;
	gpu_settings.distortion_octave_count = cpu_settings.distortion_octave_count;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainHeightLayerNoise;
	command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	command.SetGpuSettings(gpu_settings);
	return command;
}

static TerrainCommand TranslateCommandTerrainHeightLayerDistortionCpuSettings(StackAllocator* alloc, TerrainHeightLayerDistortionCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(alloc, TerrainHeightLayerDistortionGpuSettings);
	gpu_settings.type         = cpu_settings.type;
	gpu_settings.random_seed  = cpu_settings.random_seed;
	gpu_settings.inv_scale    = 1.f / cpu_settings.scale;
	gpu_settings.anisotropy   = cpu_settings.anisotropy;
	gpu_settings.rotation     = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.amplitude    = cpu_settings.amplitude;
	gpu_settings.octave_count = cpu_settings.octave_count;
	gpu_settings.lacunarity   = cpu_settings.lacunarity;
	gpu_settings.gain         = cpu_settings.gain;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainHeightLayerDistortion;
	command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	command.SetGpuSettings(gpu_settings);
	return command;
}

static TerrainCommand TranslateCommandTerrainHeightLayerStrataCpuSettings(StackAllocator* alloc, TerrainHeightLayerStrataCpuSettings& cpu_settings) {
	auto& gpu_settings = *NewFromAlloc(alloc, TerrainHeightLayerStrataGpuSettings);
	gpu_settings.random_seed          = cpu_settings.random_seed;
	gpu_settings.inv_period           = 1.f / cpu_settings.period;
	gpu_settings.tilt                 = Math::CosSin(cpu_settings.tilt * Math::degrees_to_radians);
	gpu_settings.rotation             = Math::CosSin(cpu_settings.rotation * Math::degrees_to_radians);
	gpu_settings.randomness           = cpu_settings.randomness;
	gpu_settings.amount               = cpu_settings.amount;
	gpu_settings.inv_distortion_scale = 1.f / cpu_settings.distortion_scale;
	gpu_settings.distortion_amount    = cpu_settings.distortion_amount;
	gpu_settings.octave_count         = cpu_settings.octave_count;
	gpu_settings.lacunarity           = cpu_settings.lacunarity;
	gpu_settings.gain                 = cpu_settings.gain;
	
	TerrainCommand command;
	command.type     = TerrainEditorCommandType::TerrainHeightLayerStrata;
	command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
	command.SetGpuSettings(gpu_settings);
	return command;
}

static void AppendTerrainCommand(Array<TerrainCommand>& commands, const TerrainCommand& command, u32& height_field_epoch) {
	if (command.bindings != TerrainCommandBindings::None) {
		ArrayAppend(commands, command);
	}
	
	if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
		height_field_epoch += 1;
	}
}

static ArrayView<TerrainCommand> TranslateCommands(StackAllocator* alloc, WorldEntitySystem* world_system, ArrayView<GuidComponent> layer_entity_guids) {
	Array<TerrainCommand> commands;
	ArrayReserve(commands, alloc, layer_entity_guids.count + 2);
	u32 height_field_epoch = 0;
	
	{
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::Clear;
		command.bindings = TerrainCommandBindings::DstHeight;
		AppendTerrainCommand(commands, command, height_field_epoch);
	}
	
	for (auto [layer_entity_guid] : layer_entity_guids) {
		auto layer = QueryEntityByGUID<TerrainEditorLayerSettingsQuery>(*world_system, layer_entity_guid);
		
		TerrainCommand command;
		if (layer.noise_cpu_settings != nullptr) {
			command = TranslateCommandTerrainHeightLayerNoiseCpuSettings(alloc, *layer.noise_cpu_settings);
		} else if (layer.distortion_cpu_settings != nullptr) {
			command = TranslateCommandTerrainHeightLayerDistortionCpuSettings(alloc, *layer.distortion_cpu_settings);
		} else if (layer.strata_cpu_settings != nullptr) {
			command = TranslateCommandTerrainHeightLayerStrataCpuSettings(alloc, *layer.strata_cpu_settings);
		}
		AppendTerrainCommand(commands, command, height_field_epoch);
	}
	
	if ((height_field_epoch & 0x1) == 0) {
		TerrainCommand command;
		command.type     = TerrainEditorCommandType::Copy;
		command.bindings = TerrainCommandBindings::DstHeight | TerrainCommandBindings::SrcHeight;
		AppendTerrainCommand(commands, command, height_field_epoch);
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
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::TerrainHeightField0);
	
	u32 height_field_epoch = 0;
	for (auto& command : TranslateCommands(record_context->alloc, world_system, layer_entity_guids)) {
		auto layer_constants = AllocateTransientUploadBuffer(record_context, command.gpu_settings_size);
		memcpy(layer_constants.cpu_address, command.gpu_settings, command.gpu_settings_size);
		
		RootSignature::PushConstants constants;
		constants.command_type           = command.type;
		constants.render_target_size     = render_target_size.x;
		constants.inv_render_target_size = 1.f / render_target_size.x;
		
		auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
		descriptor_table.layer_constants.Bind(layer_constants.gpu_address, sizeof(TerrainHeightLayerNoiseGpuSettings));
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::SrcHeight)) {
			descriptor_table.height_field_0 = height_field_epoch & 0x1 ? VirtualResourceID::TerrainHeightField0 : VirtualResourceID::TerrainHeightField1;
		}
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
			descriptor_table.height_field_1 = height_field_epoch & 0x1 ? VirtualResourceID::TerrainHeightField1 : VirtualResourceID::TerrainHeightField0;
		}
		
		CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
		CmdSetRootArgument(record_context, root_signature.constants, constants);
		
		CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
		
		if (HasAnyFlags(command.bindings, TerrainCommandBindings::DstHeight)) {
			height_field_epoch += 1;
		}
	}
}

