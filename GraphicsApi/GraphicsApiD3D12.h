#pragma once
#include "Basic/Basic.h"
#include "GraphicsApi.h"

#include <d3d12.h>
#include <dxgi1_4.h>

struct SwapChainBackBuffer {
	ID3D12Resource* resource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
};

struct WindowSwapChainD3D12 : WindowSwapChain {
	IDXGISwapChain3* dxgi_swap_chain = nullptr;
	
	SwapChainBackBuffer back_buffers[number_of_back_buffers] = {};
};

template<typename ResourceT>
static void SafeRelease(ResourceT*& resource) {
	if (resource) resource->Release();
	resource = nullptr;
}

struct GraphicsContextD3D12 : GraphicsContext {
	ID3D12Fence* frame_sync_fence = nullptr;
	u64 frame_index = 0;
	
	ID3D12GraphicsCommandList7* command_list = nullptr;
	ID3D12CommandAllocator* command_allocators[number_of_frames_in_flight] = {};
	
	u32* free_indices = nullptr;
	u32 free_index_count = 0;
};

