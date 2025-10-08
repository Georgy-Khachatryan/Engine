#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct ShaderCompiler;

using ShaderBytecode = FixedCountArray<ArrayView<u8>, (u32)ShaderType::Count>;

ShaderBytecode CompileShader(ShaderCompiler* compiler, StackAllocator* alloc, ShaderDefinition* definition, u64 permutation, ShaderTypeMask shader_type_mask, String root_signature_filepath);

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc);
void ReleaseShaderCompiler(ShaderCompiler* compiler);
bool CheckShaderFileChanges(ShaderCompiler* compiler, StackAllocator* alloc);
