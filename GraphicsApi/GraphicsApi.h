#pragma once
#include "Basic/Basic.h"
#include "GraphicsApiTypes.h"


compile_const u32 number_of_frames_in_flight = 2;
compile_const u32 number_of_back_buffers     = 3;
compile_const u32 persistent_srv_descriptor_count = 1024;
compile_const u32 transient_srv_descriptor_count  = 1024;


struct GraphicsContext {
	
};

struct WindowSwapChain {
	u32 width  = 0;
	u32 height = 0;
};


GraphicsContext* CreateGraphicsContext(StackAllocator* alloc);
void ReleaseGraphicsContext(GraphicsContext* context);

WindowSwapChain* CreateWindowSwapChain(StackAllocator* alloc, GraphicsContext* context, void* hwnd);
void ReleaseWindowSwapChain(WindowSwapChain* swap_chain, GraphicsContext* context);
void ResizeWindowSwapChain(WindowSwapChain* swap_chain, GraphicsContext* context, u32 width, u32 height);
void WindowSwapChainBeginFrame(WindowSwapChain* swap_chain, GraphicsContext* context, StackAllocator* alloc);
void WindowSwapChainEndFrame(WindowSwapChain* swap_chain, GraphicsContext* context, StackAllocator* alloc);

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

u32 CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation = 0);

template<typename ShadersEnumT>
u32 CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, ShadersEnumT permutation) {
	return CreateComputePipeline(lib, shader_id, (u64)permutation);
}
