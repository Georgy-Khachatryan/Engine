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

template<typename ResourceT>
static void SafeRelease(ResourceT*& resource) {
	if (resource) resource->Release();
	resource = nullptr;
}

struct GraphicsContextD3D12 : GraphicsContext {
	ID3D12Device10*     device                 = nullptr;
	ID3D12CommandQueue* graphics_command_queue = nullptr;
	
	
	FixedCountArray<ID3D12DescriptorHeap*,       (u32)DescriptorHeapType::Count> descriptor_heaps;
	FixedCountArray<u32,                         (u32)DescriptorHeapType::Count> descriptor_sizes;
	FixedCountArray<D3D12_CPU_DESCRIPTOR_HANDLE, (u32)DescriptorHeapType::Count> cpu_base_handles;
	FixedCountArray<D3D12_GPU_DESCRIPTOR_HANDLE, (u32)DescriptorHeapType::Count> gpu_base_handles;
	
	ID3D12CommandSignature* dispatch_command_signature = nullptr;
	ID3D12CommandSignature* dispatch_mesh_command_signature = nullptr;
	ID3D12CommandSignature* draw_instanced_command_signature = nullptr;
	ID3D12CommandSignature* draw_indexed_instanced_command_signature = nullptr;
	
	
	ID3D12Fence* frame_sync_fence = nullptr;
	u64 frame_index = 0;
	
	ID3D12GraphicsCommandList7* command_list = nullptr;
	FixedCountArray<ID3D12CommandAllocator*, number_of_frames_in_flight> command_allocators = {};
	
	Array<u16> srv_heap_free_indices;
	u32 srv_heap_offset = 0;
	
	Array<ID3D12RootSignature*> root_signature_table;
	Array<ID3D12PipelineState*> pipeline_state_table;
	ArrayView<PipelineDefinition> pipeline_definitions;
};

