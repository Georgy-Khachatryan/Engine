#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct ShaderCompiler;

using ShaderBytecode = FixedCountArray<ArrayView<u8>, (u32)ShaderType::Count>;
struct PipelineShaderBytecode {
	ShaderBytecode bytecode;
	bool is_dirty = false;
};

ArrayView<u64> CompileDirtyShaderPermutations(ShaderCompiler* compiler, StackAllocator* alloc);
PipelineShaderBytecode GetShadersForPipelineIndex(ShaderCompiler* compiler, u64 pipeline_definition_index, ArrayView<u64> compiled_shader_mask);

String GetShaderPermutationName(StackAllocator* alloc, ShaderDefinition* definition, u64 permutation);

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc, ArrayView<String> root_signature_filenames, ArrayView<ShaderDefinition*> shader_definitions, ArrayView<PipelineDefinition> pipeline_definitions);
void ReleaseShaderCompiler(ShaderCompiler* compiler);
bool CheckShaderFileChanges(ShaderCompiler* compiler, StackAllocator* alloc);
