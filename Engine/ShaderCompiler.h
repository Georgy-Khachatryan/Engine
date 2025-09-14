#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"

struct ShaderCompiler;
enum struct ShaderType : u32;

ArrayView<u8> CompileShader(ShaderCompiler* system, StackAllocator* alloc, String shader, ShaderType shader_type);

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc);
void ReleaseShaderCompiler(ShaderCompiler* system);

