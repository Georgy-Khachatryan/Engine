#pragma once
#include "Basic/Basic.h"
#include "GraphicsApi.h"

#define WIN32_LEAN_AND_MEAN
#include <SDK/D3D12/include/d3d12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>


compile_const u32 rtv_descriptor_count = 256;
compile_const u32 dsv_descriptor_count = 256;

enum struct DescriptorHeapType : u32 { 
	SRV = 0,
	RTV = 1,
	DSV = 2,
	
	Count
};

struct WindowSwapChainD3D12 : WindowSwapChain {
	IDXGISwapChain3* dxgi_swap_chain = nullptr;
	
	FixedCountArray<NativeTextureResource, number_of_back_buffers> back_buffers = {};
};

struct CommandQueueContextD3D12 {
	ID3D12CommandQueue* queue = nullptr;
	ID3D12Fence*        fence = nullptr;
	
	ID3D12GraphicsCommandList7* command_list = nullptr;
	FixedCountArray<ID3D12CommandAllocator*, number_of_frames_in_flight> command_allocators = {};
};

struct GraphicsContextD3D12 : GraphicsContext {
	ID3D12Device10* device = nullptr;
	
	CommandQueueContextD3D12 graphics_context;
	CommandQueueContextD3D12 async_copy_context;
	
	FixedCountArray<ID3D12DescriptorHeap*,       (u32)DescriptorHeapType::Count> descriptor_heaps;
	FixedCountArray<u32,                         (u32)DescriptorHeapType::Count> descriptor_sizes;
	FixedCountArray<D3D12_CPU_DESCRIPTOR_HANDLE, (u32)DescriptorHeapType::Count> cpu_base_handles;
	FixedCountArray<D3D12_GPU_DESCRIPTOR_HANDLE, (u32)DescriptorHeapType::Count> gpu_base_handles;
	
	ID3D12CommandSignature* dispatch_command_signature = nullptr;
	ID3D12CommandSignature* dispatch_mesh_command_signature = nullptr;
	ID3D12CommandSignature* draw_instanced_command_signature = nullptr;
	ID3D12CommandSignature* draw_indexed_instanced_command_signature = nullptr;
	
	ID3D12Fence* async_copy_fence = nullptr;
	
	Array<u16> srv_heap_free_indices;
	u32 srv_heap_offset = 0;
	
	struct ShaderCompiler* shader_compiler = nullptr;
	
	Array<ID3D12RootSignature*> root_signature_table;
	Array<ID3D12PipelineState*> pipeline_state_table;
	ArrayView<PipelineDefinition> pipeline_definitions;
	
	Array<ID3D12Pageable*> release_queue_last_frame;
	Array<ID3D12Pageable*> release_queue_this_frame;
	Array<ID3D12Pageable*> release_queue_next_frame;
};

extern void ProfilerBeginScope(const char* label, ID3D12GraphicsCommandList* command_list);
extern void ProfilerEndScope(ID3D12GraphicsCommandList* command_list);

extern void ProfilerBeginScope(const char* label, ID3D12CommandQueue* command_list);
extern void ProfilerEndScope(ID3D12CommandQueue* command_list);
