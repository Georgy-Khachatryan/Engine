#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct ShaderCompiler;

using ShaderBytecode = FixedCountArray<ArrayView<u8>, (u32)ShaderType::Count>;

ShaderBytecode CompileShadersForPipelineIndex(ShaderCompiler* compiler, StackAllocator* alloc, u64 pipeline_definition_index);

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc, ArrayView<String> root_signature_filenames, ArrayView<ShaderDefinition*> shader_definitions, ArrayView<PipelineDefinition> pipeline_definitions);
void ReleaseShaderCompiler(ShaderCompiler* compiler);
bool CheckShaderFileChanges(ShaderCompiler* compiler, StackAllocator* alloc);
