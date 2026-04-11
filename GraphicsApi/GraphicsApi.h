#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include "GraphicsApiTypes.h"


compile_const u32 number_of_back_buffers = 3;
compile_const u32 persistent_srv_descriptor_count = 1024;
compile_const u32 transient_srv_descriptor_count  = 1024;

struct RecordContext;

struct GraphicsContext {
	u64 frame_submit_index = 0; // Signaled each frame after submitting work to frame queues and then incremented.
	u64 async_submit_index = 0; // Signaled in SubmitAsyncCopyCommands after submitting work and then incremented.
};

struct WindowSwapChain {
	TextureSize size;
};


GraphicsContext* CreateGraphicsContext(StackAllocator* alloc);
void ReleaseGraphicsContext(GraphicsContext* context, StackAllocator* alloc);
void WaitForInFlightSubmits(GraphicsContext* context);

NativeTextureResource CreateTextureResource(GraphicsContext* context, TextureSize size, CreateResourceFlags flags);
NativeBufferResource CreateBufferResource(GraphicsContext* context, u32 size, CreateResourceFlags flags, u8** cpu_address = nullptr);
NativeMemoryResource CreateMemoryResource(GraphicsContext* context, u64 size);
void ReleaseTextureResource(GraphicsContext* context, NativeTextureResource resource, ResourceReleaseCondition condition = ResourceReleaseCondition::None);
void ReleaseBufferResource(GraphicsContext* context, NativeBufferResource resource, ResourceReleaseCondition condition = ResourceReleaseCondition::None);
void ReleaseMemoryResource(GraphicsContext* context, NativeMemoryResource resource, ResourceReleaseCondition condition = ResourceReleaseCondition::None);

u64 GetBufferGpuVirtualAddress(NativeBufferResource resource);

SparseTextureLayout GetSparseTextureLayout(GraphicsContext* context, NativeTextureResource resource);
MemoryRequirementsRTAS GetMeshletRtasMemoryRequirements(GraphicsContext* context, const BuildLimitsMeshletRTAS& limits);
MemoryRequirementsRTAS GetMeshletBlasMemoryRequirements(GraphicsContext* context, const BuildLimitsMeshletBLAS& limits);

void SubmitAsyncCopyCommands(GraphicsContext* context, ArrayView<AsyncCopyBufferToBufferCommand> copy_buffer_to_buffer_commands, ArrayView<AsyncCopyBufferToTextureCommand> copy_buffer_to_texture_commands, u64 async_copy_signal_index);
void AsyncUpdateMemoryMappings(GraphicsContext* context, StackAllocator* alloc, ArrayView<u32> tile_indices, u32 subresource_index, NativeTextureResource resource, NativeMemoryResource memory);
u64 GetCompletedAsyncCopyCommandValue(GraphicsContext* context);

WindowSwapChain* CreateWindowSwapChain(StackAllocator* alloc, GraphicsContext* context, void* hwnd, TextureFormat format);
void ReleaseWindowSwapChain(WindowSwapChain* swap_chain, GraphicsContext* context);
void ResizeWindowSwapChain(WindowSwapChain* swap_chain, GraphicsContext* context, uint2 size, TextureFormat format);
NativeTextureResource WindowSwapGetCurrentBackBuffer(WindowSwapChain* swap_chain);
void WindowSwapChainBeginFrame(WindowSwapChain* swap_chain, GraphicsContext* context, StackAllocator* alloc);
void WindowSwapChainEndFrame(WindowSwapChain* swap_chain, GraphicsContext* context, StackAllocator* alloc, RecordContext* record_context);

u32 AllocateTransientSrvDescriptorTable(GraphicsContext* context, u32 count);
u32 AllocatePersistentSrvDescriptor(GraphicsContext* context);
void DeallocatePersistentSrvDescriptor(GraphicsContext* context, u32 heap_index);


struct PipelineLibrary {
	StackAllocator* alloc = nullptr;
	Array<PipelineDefinition> pipeline_definitions;
	RootSignatureID current_pass_root_signature_id = { 0 };
};
using CreatePipelinesCallback = void(*)(PipelineLibrary* lib);

Array<PipelineDefinition> GatherPipelineDefinitions(StackAllocator* alloc);

PipelineID CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation = 0);
PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, ArrayView<u8> pipeline_state_stream, ShaderID shader_id, u64 permutation = 0, ShaderTypeMask shader_type_mask = ShaderTypeMask::VertexShader | ShaderTypeMask::PixelShader);
PipelineStateDescription CreatePipelineStateDescription(ArrayView<u8> stream);

template<typename ShadersEnumT>
PipelineID CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, ShadersEnumT permutation) {
	return CreateComputePipeline(lib, shader_id, (u64)permutation);
}

template<typename PipelineStateDescriptionT, typename ShadersEnumT>
PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, PipelineStateDescriptionT& pipeline_state_description, ShaderID shader_id, ShadersEnumT permutation, ShaderTypeMask shader_type_mask = ShaderTypeMask::VertexShader | ShaderTypeMask::PixelShader) {
	return CreateGraphicsPipeline(lib, { (u8*)&pipeline_state_description, sizeof(pipeline_state_description) }, shader_id, (u64)permutation, shader_type_mask);
}

template<typename PipelineStateDescriptionT>
PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, PipelineStateDescriptionT& pipeline_state_description, ShaderID shader_id) {
	return CreateGraphicsPipeline(lib, { (u8*)&pipeline_state_description, sizeof(pipeline_state_description) }, shader_id);
}

struct VirtualResourceTable {
	Array<VirtualResource> virtual_resources;
	
	void Set(VirtualResourceID resource_id, TextureSize size, CreateResourceFlags flags = CreateResourceFlags::UAV) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type  = VirtualResource::Type::VirtualTexture;
		resource.flags = flags;
		resource.texture.size = size;
	}
	
	void Set(VirtualResourceID resource_id, u32 size, CreateResourceFlags flags = CreateResourceFlags::UAV) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type  = VirtualResource::Type::VirtualBuffer;
		resource.flags = flags;
		resource.buffer.size = size;
	}
	
	void Set(VirtualResourceID resource_id, NativeTextureResource native_resource, TextureSize size) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type  = VirtualResource::Type::NativeTexture;
		resource.flags = CreateResourceFlags::None;
		resource.texture.resource       = native_resource;
		resource.texture.size           = size;
		resource.texture.allocated_size = size;
	}
	
	void Set(VirtualResourceID resource_id, NativeBufferResource native_resource, u32 size, u8* cpu_address = nullptr) {
		auto& resource = virtual_resources[(u32)resource_id];
		resource.type  = VirtualResource::Type::NativeBuffer;
		resource.flags = CreateResourceFlags::None;
		resource.buffer.resource       = native_resource;
		resource.buffer.size           = size;
		resource.buffer.allocated_size = size;
		resource.buffer.cpu_address    = cpu_address;
	}
	
	VirtualResourceID AddTransient(NativeTextureResource native_resource, TextureSize size) {
		auto resource_id = (VirtualResourceID)virtual_resources.count;
		
		auto& resource = ArrayEmplace(virtual_resources);
		resource.type  = VirtualResource::Type::NativeTexture;
		resource.flags = CreateResourceFlags::None;
		resource.texture.resource       = native_resource;
		resource.texture.size           = size;
		resource.texture.allocated_size = size;
		
		return resource_id;
	}
	
	VirtualResourceID AddTransient(NativeBufferResource native_resource, u32 size) {
		auto resource_id = (VirtualResourceID)virtual_resources.count;
		
		auto& resource = ArrayEmplace(virtual_resources);
		resource.type  = VirtualResource::Type::NativeBuffer;
		resource.flags = CreateResourceFlags::None;
		resource.buffer.resource       = native_resource;
		resource.buffer.size           = size;
		resource.buffer.allocated_size = size;
		
		return resource_id;
	}
};

