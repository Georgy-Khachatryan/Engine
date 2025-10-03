#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"


union NativeBufferResource {
	struct ID3D12Resource* d3d12 = nullptr;
};

union NativeTextureResource {
	struct ID3D12Resource* d3d12 = nullptr;
};


enum struct ShaderType : u32 {
	ComputeShader = 0,
	VertexShader  = 1,
	PixelShader   = 2,
	
	Count
};

enum struct ShaderTypeMask : u32 {
	None          = 0u,
	ComputeShader = 1u << (u32)ShaderType::ComputeShader,
	VertexShader  = 1u << (u32)ShaderType::VertexShader,
	PixelShader   = 1u << (u32)ShaderType::PixelShader,
};
ENUM_FLAGS_OPERATORS(ShaderTypeMask);

struct ShaderDefinition {
	String filename;
	ArrayView<String> defines;
	
	struct ShaderPermutationTable* shader_table = nullptr;
};


struct ShaderID { u32 index = 0; };

