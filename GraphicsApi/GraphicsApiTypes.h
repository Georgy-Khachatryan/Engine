#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "TextureFormat.h"

union NativeBufferResource {
	void* handle = nullptr;
	struct ID3D12Resource* d3d12;
};

union NativeTextureResource {
	void* handle = nullptr;
	struct ID3D12Resource* d3d12;
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


struct GpuAddress {
	VirtualResourceID resource_id = {};
	u32 offset = 0;
	
	GpuAddress() : resource_id{}, offset(0) {}
	GpuAddress(VirtualResourceID resource_id, u32 offset = 0) : resource_id(resource_id), offset(offset) {}
	GpuAddress(const GpuAddress& other) = default;
};

struct VirtualResource {
	enum struct Type : u32 {
		None           = 0,
		NativeBuffer   = 1,
		NativeTexture  = 2,
		VirtualBuffer  = 3,
		VirtualTexture = 4,
	};
	
	Type type = Type::None;
	u32 padding_0 = 0;
	
	union {
		struct {
			NativeTextureResource resource;
			TextureSize size;
			TextureSize allocated_size;
		} texture;
		
		struct {
			NativeBufferResource resource;
			u8* cpu_address;
			u32 size;
			u32 allocated_size;
		} buffer;
	};
};


enum struct PipelineStateType : u8 {
	BlendState   = 0,
	RenderTarget = 1,
	DepthStencil = 2,
	Rasterizer   = 3,
};

struct PipelineBlendState {
	enum struct Blend : u8 {
		Zero        = 0,
		One         = 1,
		SrcAlpha    = 2,
		InvSrcAlpha = 3,
		DstAlpha    = 4,
		InvDstAlpha = 5,
		
		Count
	};
	
	enum struct BlendOp : u8 {
		Add    = 0,
		Sub    = 1,
		RevSub = 2,
		Min    = 3,
		Max    = 4,
		
		Count
	};
	
	enum struct WriteMask : u8 {
		None = 0,
		R    = 1u << 0,
		G    = 1u << 1,
		B    = 1u << 2,
		A    = 1u << 3,
		RG   = R | G,
		RGB  = R | G | B,
		RGBA = R | G | B | A,
	};
	
	PipelineStateType type = PipelineStateType::BlendState;
	
	WriteMask write_mask = WriteMask::RGBA;
	
	Blend  src_blend_rgb = Blend::One;
	Blend  dst_blend_rgb = Blend::Zero;
	BlendOp blend_op_rgb = BlendOp::Add;
	
	Blend  src_blend_a = Blend::One;
	Blend  dst_blend_a = Blend::Zero;
	BlendOp blend_op_a = BlendOp::Add;
};

struct PipelineRenderTarget {
	PipelineStateType type = PipelineStateType::RenderTarget;
	TextureFormat format = TextureFormat::None;
};

struct PipelineDepthStencil {
	enum struct ComparisonMode : u8 {
		None         = 0,
		Always       = 1,
		Never        = 2,
		Greater      = 3,
		Less         = 4,
		GreaterEqual = 5,
		LessEqual    = 6,
		Equal        = 7,
		NotEqual     = 8,
		
		Count
	};
	
	enum struct StencilOp : u8 {
		Keep              = 0,
		Zero              = 1,
		Replace           = 2,
		Invert            = 3,
		Increment         = 4,
		Decrement         = 5,
		IncrementSaturate = 6,
		DecrementSaturate = 7,
		
		Count
	};
	
	enum struct Flags : u8 {
		None             = 0,
		EnableDepth      = 1u << 0,
		EnableDepthWrite = 1u << 1,
		EnableStencil    = 1u << 2,
	};
	
	struct StencilFaceOps {
		ComparisonMode stencil_comparison = ComparisonMode::Never;
		StencilOp stencil_pass_depth_pass = StencilOp::Keep; // Both stencil and depth test passed.
		StencilOp stencil_pass_depth_fail = StencilOp::Keep; // Stencil test passed, but depth test failed.
		StencilOp stencil_fail_depth_none = StencilOp::Keep; // Stencil test failed, depth test wasn't performed.
	};
	
	PipelineStateType type = PipelineStateType::DepthStencil;
	
	Flags          flags            = Flags::EnableDepth;
	TextureFormat  format           = TextureFormat::None;
	ComparisonMode depth_comparison = ComparisonMode::GreaterEqual;
	
	u8 stencil_read_mask  = 0;
	u8 stencil_write_mask = 0;
	
	StencilFaceOps front = {};
	StencilFaceOps back  = {};
};
ENUM_FLAGS_OPERATORS(PipelineDepthStencil::Flags);

struct PipelineRasterizer {
	enum struct CullMode : u8 {
		None  = 0,
		Front = 1,
		Back  = 2,
		
		Count
	};
	
	enum struct FrontFaceWinding : u8 {
		CCW = 0,
		CW  = 1,
	};
	
	PipelineStateType type = PipelineStateType::Rasterizer;
	
	CullMode cull_mode = CullMode::None;
	FrontFaceWinding front_face_winding = FrontFaceWinding::CCW;
};

struct PipelineStateDescription {
	FixedCapacityArray<const PipelineBlendState*,   8> blend_states;
	FixedCapacityArray<const PipelineRenderTarget*, 8> render_targets;
	const PipelineDepthStencil* depth_stencil = nullptr;
	const PipelineRasterizer*   rasterizer    = nullptr;
};

