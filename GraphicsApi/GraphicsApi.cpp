#include "GraphicsApi.h"

u32 CreateComputePipeline(PipelineLibrary* lib, ShaderDefinition& shader_definition, u64 permutation) {
	u32 pipeline_index = (u32)lib->pipeline_definitions.count;
	
	auto& pipeline_definition = ArrayEmplace(lib->pipeline_definitions, lib->alloc);
	pipeline_definition.shader_definition    = &shader_definition;
	pipeline_definition.permutation          = permutation;
	pipeline_definition.shader_type_mask     = ShaderTypeMask::ComputeShader;
	pipeline_definition.root_signature_index = lib->current_pass_root_signature_index;
	
	return pipeline_index;
}
