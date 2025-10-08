#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include "GraphicsApiTypes.h"


compile_const u32 number_of_frames_in_flight = 2;
compile_const u32 number_of_back_buffers     = 3;
compile_const u32 persistent_srv_descriptor_count = 1024;
compile_const u32 transient_srv_descriptor_count  = 1024;

struct RecordContext;

struct GraphicsContext {
	
};

struct WindowSwapChain {
	uint2 size = 0;
};


GraphicsContext* CreateGraphicsContext(StackAllocator* alloc);
void ReleaseGraphicsContext(GraphicsContext* context);

NativeTextureResource CreateTextureResource(GraphicsContext* context, TextureSize size);
NativeBufferResource CreateBufferResource(GraphicsContext* context, u32 size, u8** cpu_address);

WindowSwapChain* CreateWindowSwapChain(StackAllocator* alloc, GraphicsContext* context, void* hwnd);
void ReleaseWindowSwapChain(WindowSwapChain* swap_chain, GraphicsContext* context);
void ResizeWindowSwapChain(WindowSwapChain* swap_chain, GraphicsContext* context, uint2 size);
NativeTextureResource WindowSwapGetCurrentBackBuffer(WindowSwapChain* swap_chain);
void WindowSwapChainBeginFrame(WindowSwapChain* swap_chain, GraphicsContext* context, StackAllocator* alloc);
void WindowSwapChainEndFrame(WindowSwapChain* swap_chain, GraphicsContext* context, StackAllocator* alloc, RecordContext& record_context);

u32 AllocateTransientSrvDescriptorTable(GraphicsContext* api_context, u32 count);


struct PipelineDefinition {
	ShaderDefinition* shader_definition = nullptr;
	u64               permutation       = 0;
	ShaderTypeMask    shader_type_mask  = ShaderTypeMask::None;
	u32               root_signature_index = 0;
};

struct PipelineLibrary {
	StackAllocator* alloc = nullptr;
	Array<PipelineDefinition> pipeline_definitions;
	u32 current_pass_root_signature_index = 0;
};

PipelineID CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation = 0);
PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation = 0, ShaderTypeMask shader_type_mask = ShaderTypeMask::VertexShader | ShaderTypeMask::PixelShader);

template<typename ShadersEnumT>
PipelineID CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, ShadersEnumT permutation) {
	return CreateComputePipeline(lib, shader_id, (u64)permutation);
}

template<typename ShadersEnumT>
PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, ShaderID shader_id, ShadersEnumT permutation, ShaderTypeMask shader_type_mask = ShaderTypeMask::VertexShader | ShaderTypeMask::PixelShader) {
	return CreateComputePipeline(lib, shader_id, (u64)permutation, shader_type_mask);
}


enum struct VirtualResourceID : u32;

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


struct VirtualResourceTable {
	Array<VirtualResource> virtual_resources;
	
	void Set(VirtualResourceID resource_id, TextureSize size) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type = VirtualResource::Type::VirtualTexture;
		resource.texture.size = size;
	}
	
	void Set(VirtualResourceID resource_id, u32 size) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type = VirtualResource::Type::VirtualBuffer;
		resource.buffer.size = size;
	}
	
	void Set(VirtualResourceID resource_id, NativeTextureResource native_resource, TextureSize size) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type = VirtualResource::Type::NativeTexture;
		resource.texture.resource       = native_resource;
		resource.texture.size           = size;
		resource.texture.allocated_size = size;
	}
	
	void Set(VirtualResourceID resource_id, NativeBufferResource native_resource, u32 size, u8* cpu_address = nullptr) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type = VirtualResource::Type::NativeBuffer;
		resource.buffer.resource       = native_resource;
		resource.buffer.size           = size;
		resource.buffer.allocated_size = size;
		resource.buffer.cpu_address    = cpu_address;
	}
};

