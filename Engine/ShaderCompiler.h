#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct ShaderCompiler;

FixedCountArray<ArrayView<u8>, (u32)ShaderType::Count> CompileShader(ShaderCompiler* compiler, ShaderDefinition* definition, u64 permutation, ShaderTypeMask shader_type_mask);

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc);
void ReleaseShaderCompiler(ShaderCompiler* compiler);
bool CheckShaderFileChanges(ShaderCompiler* compiler);
