#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "TextureFormat.h"

compile_const u32 number_of_frames_in_flight = 2;
compile_const u32 gpu_memory_page_size_bits  = 16;
compile_const u32 gpu_memory_page_size       = 1u << gpu_memory_page_size_bits;
compile_const u32 rtas_alignment             = 256;


enum struct VirtualResourceID : u32;
struct RecordContext;
struct PipelineLibrary;
struct GraphicsContext;


union NativeBufferResource {
	void* handle = nullptr;
	struct ID3D12Resource* d3d12;
};

union NativeTextureResource {
	void* handle = nullptr;
	struct ID3D12Resource* d3d12;
};

union NativeMemoryResource {
	void* handle = nullptr;
	struct ID3D12Heap* d3d12;
};

struct GpuAddress {
	VirtualResourceID resource_id = {};
	u32 offset = 0;
	
	GpuAddress() : resource_id{}, offset(0) {}
	GpuAddress(VirtualResourceID resource_id, u32 offset = 0) : resource_id(resource_id), offset(offset) {}
	GpuAddress(const GpuAddress& other) = default;
};


enum struct ShaderType : u32 {
	ComputeShader = 0,
	VertexShader  = 1,
	PixelShader   = 2,
	MeshShader    = 3,
	
	Count
};

enum struct ShaderTypeMask : u32 {
	None          = 0u,
	ComputeShader = 1u << (u32)ShaderType::ComputeShader,
	VertexShader  = 1u << (u32)ShaderType::VertexShader,
	PixelShader   = 1u << (u32)ShaderType::PixelShader,
	MeshShader    = 1u << (u32)ShaderType::MeshShader,
};
ENUM_FLAGS_OPERATORS(ShaderTypeMask);


struct ShaderDefinition {
	String filename;
	ArrayView<String> defines;
};

struct ShaderID {
	u32 index = 0;
};

struct RootSignatureID {
	u32 index = 0;
};


struct PipelineDefinition {
	ShaderID        shader_id         = { 0 };
	RootSignatureID root_signature_id = { 0 };
	ShaderTypeMask  shader_type_mask  = ShaderTypeMask::None;
	u64             permutation       = 0;
	ArrayView<u8>   pipeline_state_stream = {};
};


struct MemoryRequirementsRTAS {
	u32 rtas_max_size_bytes = 0;
	u32 scratch_size_bytes  = 0;
};

struct BuildLimitsMeshletRTAS {
	u32 max_meshlet_count        = 0;
	u32 max_total_triangle_count = 0;
	u32 max_total_vertex_count	 = 0;
};

struct BuildLimitsMeshletBLAS {
	u32 max_blas_count          = 0;
	u32 max_total_meshlet_count = 0;
	u32 max_meshlets_per_blas   = 0;
};

struct BuildInputsMeshletRTAS {
	BuildLimitsMeshletRTAS limits;
	GpuAddress meshlet_rtas;
	GpuAddress scratch_data;
	GpuAddress meshlet_descs;
	GpuAddress indirect_arguments;
};

struct BuildInputsMeshletBLAS {
	BuildLimitsMeshletBLAS limits;
	GpuAddress meshlet_blas;
	GpuAddress scratch_data;
	GpuAddress blas_descs;
	GpuAddress indirect_arguments;
	GpuAddress indirect_argument_count;
};


NOTES()
enum struct CommandQueueType : u32 {
	Graphics = 0,
	Compute  = 1,
	
	Count
};

enum struct PipelineStagesMask : u16 {
	None              = 0,
	ComputeShader     = 1u << 0,
	VertexShader      = 1u << 1,
	PixelShader       = 1u << 2,
	AnyShader         = ComputeShader | VertexShader | PixelShader,
	Copy              = 1u << 3,
	RenderTarget      = 1u << 4,
	DepthStencilRO    = 1u << 5,
	DepthStencilRW    = 1u << 6,
	DepthStencil      = DepthStencilRO | DepthStencilRW,
	IndirectArguments = 1u << 7,
	RtasBuild         = 1u << 8,
};
ENUM_FLAGS_OPERATORS(PipelineStagesMask);

enum struct ResourceAccessMask : u16 {
	None              = 0,
	SRV               = 1u << 0,
	UAV               = 1u << 1,
	CopySrc           = 1u << 2,
	CopyDst           = 1u << 3,
	RenderTarget      = 1u << 4,
	DepthStencilRO    = 1u << 5,
	DepthStencilRW    = 1u << 6,
	IndirectArguments = 1u << 7,
	RtasRO            = 1u << 8,
	RtasRW            = 1u << 9,
	AccessRO          = SRV | CopySrc | DepthStencilRO | IndirectArguments,
	AccessRW          = UAV | CopyDst | DepthStencilRW | RenderTarget,
};
ENUM_FLAGS_OPERATORS(ResourceAccessMask);

enum struct DepthStencilAccess : u8 {
	None          = 0,
	DepthRead     = 1u << 0,
	DepthWrite    = 1u << 1,
	DepthAccess   = DepthRead | DepthWrite,
	StencilRead   = 1u << 2,
	StencilWrite  = 1u << 3,
	StencilAccess = StencilRead | StencilWrite,
};
ENUM_FLAGS_OPERATORS(DepthStencilAccess);

struct PipelineID {
	u32 index = 0;
	
	PipelineStagesMask stages_mask = PipelineStagesMask::None;
	DepthStencilAccess depth_stencil_access = DepthStencilAccess::None;
};

enum struct ResourceAccessFlags : u8 {
	None                 = 0,
	IsTexture            = 1u << 0,
	IsFullResourceAccess = 1u << 1,
	IsErased             = 1u << 2,
};
ENUM_FLAGS_OPERATORS(ResourceAccessFlags);

struct ResourceAccessDefinition {
	ResourceAccessDefinition* last_access = nullptr;
	VirtualResourceID resource_id = (VirtualResourceID)0;
	
	PipelineStagesMask stages_mask = PipelineStagesMask::None;
	ResourceAccessMask access_mask = ResourceAccessMask::None;
	
	ResourceAccessFlags flags = ResourceAccessFlags::None;
	u8 plane_mask = 0;
	
	u8 mip_index = 0;
	u8 mip_count = 0;
	
	u16 array_index = 0;
	u16 array_count = 0;
};
static_assert(sizeof(ResourceAccessDefinition) == 24, "Incorrect ResourceAccessDefinition size.");


enum struct CreateResourceFlags : u32 {
	None     = 0,
	Upload   = 1u << 0,
	Readback = 1u << 1,
	DSV      = 1u << 2,
	RTV      = 1u << 3,
	UAV      = 1u << 4,
	RTAS     = 1u << 5,
	Sparse   = 1u << 6,
};
ENUM_FLAGS_OPERATORS(CreateResourceFlags);

struct VirtualResource {
	enum struct Type : u32 {
		None           = 0,
		NativeBuffer   = 1,
		NativeTexture  = 2,
		VirtualBuffer  = 3,
		VirtualTexture = 4,
		Opaque         = 5,
	};
	
	Type type = Type::None;
	CreateResourceFlags flags = CreateResourceFlags::None;
	
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
		
		struct {
			void* user_data_0 = nullptr;
			u64   user_data_1 = 0;
			void (*release_user_data)(VirtualResource*, GraphicsContext*) = nullptr;
		} opaque;
	};
};


enum struct ResourceReleaseCondition : u32 {
	None              = 0,
	EndOfLastGpuFrame = 1,
	EndOfThisGpuFrame = 2,
	EndOfNextGpuFrame = 3,
};


NOTES()
enum struct ResourceDescriptorType : u16 {
	AnyTexture  = 1u << 0,
	AnyBuffer   = 1u << 1,
	AnySRV      = 1u << 2,
	AnyUAV      = 1u << 3,
	IndexOffset = 4,
	
	None            = (0u << IndexOffset),
	Texture2D       = (1u << IndexOffset) | AnyTexture | AnySRV,
	RWTexture2D     = (2u << IndexOffset) | AnyTexture | AnyUAV,
	RegularBuffer   = (3u << IndexOffset) | AnyBuffer  | AnySRV,
	RWRegularBuffer = (4u << IndexOffset) | AnyBuffer  | AnyUAV,
	ByteBuffer      = (5u << IndexOffset) | AnyBuffer  | AnySRV,
	RWByteBuffer    = (6u << IndexOffset) | AnyBuffer  | AnyUAV,
};
ENUM_FLAGS_OPERATORS(ResourceDescriptorType);

struct ResourceDescriptor {
	using Type = ResourceDescriptorType;
	VirtualResourceID resource_id;
	
	union {
		struct {
			Type type = Type::None;
		} common = {};
		
		struct {
			Type type;
			u16 stride;
			
			u32 offset;
			u32 size;
		} buffer;
		
		struct {
			Type type;
			
			u8 mip_index;
			u8 mip_count;
			
			u16 array_index;
			u16 array_count;
			
			u32 padding_2;
		} texture;
	};
};
static_assert(sizeof(ResourceDescriptor) == 16, "Incorrect ResourceDescriptor size.");


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
		EnableDepthWrite = EnableDepth | (1u << 1),
		EnableStencil    = 1u << 2,
	};
	
	struct StencilFaceOps {
		ComparisonMode stencil_comparison = ComparisonMode::Never;
		StencilOp stencil_pass_depth_pass = StencilOp::Keep; // Both stencil and depth test passed.
		StencilOp stencil_pass_depth_fail = StencilOp::Keep; // Stencil test passed, but depth test failed.
		StencilOp stencil_fail_depth_none = StencilOp::Keep; // Stencil test failed, depth test wasn't performed.
	};
	
	PipelineStateType type = PipelineStateType::DepthStencil;
	
	Flags          flags            = Flags::None;
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


struct GpuReadbackQueueElement {
	u8* data = nullptr;
	u64 frame_index = 0;
};

struct GpuReadbackQueue {
	FixedCountArray<GpuReadbackQueueElement, number_of_frames_in_flight * 2> elements;
	
	void Store(u8* data, u64 frame_index) {
		auto& element = elements[(frame_index + number_of_frames_in_flight) % elements.capacity];
		element.data = data;
		element.frame_index = frame_index;
	}
	
	GpuReadbackQueueElement Load(u64 frame_index) {
		auto& element = elements[frame_index % elements.capacity];
		bool is_valid_element = (element.frame_index + number_of_frames_in_flight) == frame_index;
		return is_valid_element ? element : GpuReadbackQueueElement{};
	}
};

struct AsyncCopyBufferToBufferCommand {
	NativeBufferResource src_resource;
	NativeBufferResource dst_resource;
	u64 src_offset = 0;
	u64 dst_offset = 0;
	u64 size = 0;
};

struct AsyncCopyBufferToTextureCommand {
	NativeBufferResource src_resource;
	NativeTextureResource dst_resource;
	
	TextureFormat format;
	
	u32 src_row_pitch = 0;
	uint3 src_size = 0;
	u64 src_offset = 0;
	
	u32 dst_subresource_index = 0;
	uint3 dst_offset = 0;
};


namespace Meta {
	NOTES() struct RenderPass { CommandQueueType pass_type = CommandQueueType::Compute; };
	NOTES() struct HlslFile { String filename; };
	NOTES() struct ShaderName { String filename; };
};


NOTES()
enum struct RootArgumentType : u32 {
	DescriptorTable    = 0,
	ConstantBuffer     = 1,
	PushConstantBuffer = 2,
};

namespace HLSL {
	NOTES(ResourceDescriptorType::Texture2D)
	template<typename T>
	struct Texture2D : ResourceDescriptor {
		Texture2D(VirtualResourceID resource = (VirtualResourceID)0, u32 mip_offset = 0, u32 mip_count = u32_max) { Bind(resource, mip_offset, mip_count); }
		
		void Bind(VirtualResourceID resource, u32 mip_offset = 0, u32 mip_count = u32_max) {
			resource_id = resource;
			texture = { Type::Texture2D, (u8)mip_offset, (u8)mip_count, 0, 1, 0 };
		}
	};
	
	NOTES(ResourceDescriptorType::RWTexture2D)
	template<typename T>
	struct RWTexture2D : ResourceDescriptor {
		RWTexture2D(VirtualResourceID resource = (VirtualResourceID)0, u32 mip_index = 0) { Bind(resource, mip_index); }
		
		void Bind(VirtualResourceID resource, u32 mip_index = 0) {
			resource_id = resource;
			texture = { Type::RWTexture2D, (u8)mip_index, 1, 0, 1, 0 };
		}
	};
	
	NOTES(ResourceDescriptorType::RegularBuffer)
	template<typename T>
	struct RegularBuffer : ResourceDescriptor {
		RegularBuffer(VirtualResourceID resource = (VirtualResourceID)0, u32 size = u32_max) { Bind(resource, size); }
		
		void Bind(GpuAddress gpu_address, u32 size = u32_max) {
			resource_id = gpu_address.resource_id;
			buffer = { Type::RegularBuffer, (u16)sizeof(T), gpu_address.offset, size };
		}
	};
	
	NOTES(ResourceDescriptorType::RWRegularBuffer)
	template<typename T>
	struct RWRegularBuffer : ResourceDescriptor {
		RWRegularBuffer(VirtualResourceID resource = (VirtualResourceID)0, u32 size = u32_max) { Bind(resource, size); }
		
		void Bind(GpuAddress gpu_address, u32 size = u32_max) {
			resource_id = gpu_address.resource_id;
			buffer = { Type::RWRegularBuffer, (u16)sizeof(T), gpu_address.offset, size };
		}
	};
	
	NOTES(ResourceDescriptorType::ByteBuffer)
	struct ByteBuffer : ResourceDescriptor {
		ByteBuffer(VirtualResourceID resource = (VirtualResourceID)0, u32 size = u32_max) { Bind(resource, size); }
		
		void Bind(GpuAddress gpu_address, u32 size = u32_max) {
			resource_id = gpu_address.resource_id;
			buffer = { Type::ByteBuffer, 1, gpu_address.offset, size };
		}
	};
	
	NOTES(ResourceDescriptorType::RWByteBuffer)
	struct RWByteBuffer : ResourceDescriptor {
		RWByteBuffer(VirtualResourceID resource = (VirtualResourceID)0, u32 size = u32_max) { Bind(resource, size); }
		
		void Bind(GpuAddress gpu_address, u32 size = u32_max) {
			resource_id = gpu_address.resource_id;
			buffer = { Type::RWByteBuffer, 1, gpu_address.offset, size };
		}
	};
	
	
	NOTES(RootArgumentType::DescriptorTable)
	template<typename T>
	struct DescriptorTable {
		u32 offset = 0;
		u32 descriptor_count = 0;
	};
	
	NOTES(RootArgumentType::ConstantBuffer)
	template<typename T>
	struct ConstantBuffer {
		u32 offset = 0;
	};
	
	NOTES(RootArgumentType::PushConstantBuffer)
	template<typename T>
	struct PushConstantBuffer {
		u32 offset = 0;
	};
	
	
	struct BaseRootSignature {
		RootSignatureID root_signature_id = { 0 };
		u32 root_parameter_count = 0;
		CommandQueueType pass_type = CommandQueueType::Graphics;
	};
	
	struct BaseDescriptorTable {
		u32 descriptor_heap_offset = 0;
		u32 descriptor_count = 0;
	};
};


#define RENDER_PASS_GENERATED_CODE()\
	struct RootSignature;\
	static RootSignature root_signature;\
	static String debug_name;\
	static void CreatePipelines(PipelineLibrary* lib);\
	void RecordPass(RecordContext* record_context)

#define SHADER_DEFINITION_GENERATED_CODE(name)\
	extern ShaderID name##ID;\
	ENUM_FLAGS_OPERATORS(name)

