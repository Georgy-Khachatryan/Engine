#include "GraphicsApi.h"

extern ArrayView<ShaderDefinition*> shader_definition_table;

PipelineID CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation) {
	u32 pipeline_index = (u32)lib->pipeline_definitions.count;
	
	auto& pipeline_definition = ArrayEmplace(lib->pipeline_definitions, lib->alloc);
	pipeline_definition.shader_definition    = shader_definition_table[shader_id.index];
	pipeline_definition.permutation          = permutation;
	pipeline_definition.shader_type_mask     = ShaderTypeMask::ComputeShader;
	pipeline_definition.root_signature_index = lib->current_pass_root_signature_index;
	
	return PipelineID{ pipeline_index };
}

PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation, ShaderTypeMask shader_type_mask) {
	u32 pipeline_index = (u32)lib->pipeline_definitions.count;
	
	auto& pipeline_definition = ArrayEmplace(lib->pipeline_definitions, lib->alloc);
	pipeline_definition.shader_definition    = shader_definition_table[shader_id.index];
	pipeline_definition.permutation          = permutation;
	pipeline_definition.shader_type_mask     = shader_type_mask;
	pipeline_definition.root_signature_index = lib->current_pass_root_signature_index;
	
	return PipelineID{ pipeline_index };
}

