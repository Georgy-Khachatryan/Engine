#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "TextureFormat.h"

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
struct PipelineID { u32 index = 0; };
enum struct VirtualResourceID : u32;


enum struct PipelineStagesMask : u16 {
	None          = 0,
	ComputeShader = 1u << 0,
	VertexShader  = 1u << 1,
	PixelShader   = 1u << 2,
	Copy          = 1u << 3,
	RenderTarget  = 1u << 4,
};
ENUM_FLAGS_OPERATORS(PipelineStagesMask);

enum struct ResourceAccessMask : u16 {
	None         = 0,
	SRV          = 1u << 0,
	UAV          = 1u << 1,
	CopySrc      = 1u << 2,
	CopyDst      = 1u << 3,
	RenderTarget = 1u << 4,
};
ENUM_FLAGS_OPERATORS(ResourceAccessMask);

struct ResourceAccessDefinition {
	ResourceAccessDefinition* last_access = nullptr;
	VirtualResourceID resource_id;
	
	u16                is_texture  : 1;
	PipelineStagesMask stages_mask : 7;
	ResourceAccessMask access_mask : 8;
	
	u8 mip_index = 0;
	u8 mip_count = 0;
	
	u16 array_index = 0;
	u16 array_count = 0;
};
static_assert(sizeof(ResourceAccessDefinition) == 24, "Incorrect ResourceAccessDefinition size.");

