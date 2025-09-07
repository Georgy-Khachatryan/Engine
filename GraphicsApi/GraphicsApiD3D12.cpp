#include "GraphicsApi.h"
#include "Basic/BasicMemory.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <SDK/imgui/backends/imgui_impl_dx12.h>

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

static void WaitForLastFrame(GraphicsContextD3D12* context) {
	if (context->frame_index < 1) return;
	context->frame_sync_fence->SetEventOnCompletion(context->frame_index - 1, nullptr);
}

static void WaitForNextFrame(GraphicsContextD3D12* context) {
	if (context->frame_index < number_of_frames_in_flight) return;
	context->frame_sync_fence->SetEventOnCompletion(context->frame_index - number_of_frames_in_flight, nullptr);
}

GraphicsContext* CreateGraphicsContext(StackAllocator* alloc) {
	auto* context = NewFromAlloc(alloc, GraphicsContextD3D12);
	
	ID3D12Device4* device = nullptr;
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)))) {
		DebugAssertAlways("D3D12CreateDevice failed.");
		return nullptr;
	}
	context->device.d3d12 = device;
	
	
	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.NumDescriptors = 256;
		heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.NodeMask       = 0;
		
		ID3D12DescriptorHeap* resource_descriptor_heap = nullptr;
		if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&resource_descriptor_heap)))) {
			DebugAssertAlways("CreateDescriptorHeap failed.");
			return nullptr;
		}
		context->resource_descriptor_heap.d3d12 = resource_descriptor_heap;
		
		u32* free_indices = (u32*)alloc->Allocate(heap_desc.NumDescriptors * sizeof(u32), alignof(u32));
		context->free_indices     = free_indices;
		context->free_index_count = heap_desc.NumDescriptors;
		for (u32 i = 0; i < heap_desc.NumDescriptors; i += 1) {
			free_indices[i] = i;
		}
	}
	
	
	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heap_desc.NumDescriptors = 256;
		heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heap_desc.NodeMask       = 0;
		
		ID3D12DescriptorHeap* rtv_descriptor_heap = nullptr;
		if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_descriptor_heap)))) {
			DebugAssertAlways("CreateDescriptorHeap failed.");
			return nullptr;
		}
		context->rtv_descriptor_heap.d3d12 = rtv_descriptor_heap;
	}
	
	
	{
		D3D12_COMMAND_QUEUE_DESC queue_desc = {};
		queue_desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queue_desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queue_desc.NodeMask = 0;
		
		ID3D12CommandQueue* queue = nullptr;
		if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) {
			DebugAssertAlways("CreateCommandQueue failed.");
			return nullptr;
		}
		context->graphics_command_queue.d3d12 = queue;
	}
	
	
	{
		ID3D12Fence* fence = nullptr;
		if (device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) {
			DebugAssertAlways("CreateFence failed.");
			return nullptr;
		}
		context->frame_sync_fence = fence;
		context->frame_index = 1;
	}
	
	
	for (auto& command_allocator : context->command_allocators) {
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)))) {
			DebugAssertAlways("CreateCommandAllocator failed.");
			return nullptr;
		}
	}
	
	
	{
		ID3D12GraphicsCommandList7* command_list = nullptr;
		if (FAILED(device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&command_list)))) {
			DebugAssertAlways("CreateCommandList failed.");
			return nullptr;
		}
		context->command_list = command_list;
	}
	
	{
		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device            = device;
		init_info.CommandQueue      = context->graphics_command_queue.d3d12;
		init_info.NumFramesInFlight = number_of_frames_in_flight;
		init_info.RTVFormat         = DXGI_FORMAT_R8G8B8A8_UNORM;
		init_info.DSVFormat         = DXGI_FORMAT_UNKNOWN;
		init_info.UserData          = context;
		init_info.SrvDescriptorHeap = context->resource_descriptor_heap.d3d12;
		
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
			auto* context = (GraphicsContextD3D12*)init_info->UserData;
			
			DebugAssert(context->free_index_count != 0, "Resource descriptor heap is exhausted.");
			u32 index = context->free_indices[--context->free_index_count];
			
			auto* resource_descriptor_heap = context->resource_descriptor_heap.d3d12;
			u64 cpu_heap_base = resource_descriptor_heap->GetCPUDescriptorHandleForHeapStart().ptr;
			u64 gpu_heap_base = resource_descriptor_heap->GetGPUDescriptorHandleForHeapStart().ptr;
			u32 heap_increment = context->device.d3d12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			
			out_cpu_desc_handle->ptr = cpu_heap_base + index * heap_increment;
			out_gpu_desc_handle->ptr = gpu_heap_base + index * heap_increment;
		};
		
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE) {
			auto* context = (GraphicsContextD3D12*)init_info->UserData;
			
			auto* resource_descriptor_heap = context->resource_descriptor_heap.d3d12;
			u64 cpu_heap_base  = resource_descriptor_heap->GetCPUDescriptorHandleForHeapStart().ptr;
			u32 heap_increment = context->device.d3d12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			
			u32 index = (u32)((cpu_desc_handle.ptr - cpu_heap_base) / heap_increment);
			context->free_indices[context->free_index_count++] = index;
		};
		ImGui_ImplDX12_Init(&init_info);
	}
	
	return context;
}

void ReleaseGraphicsContext(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;

	ImGui_ImplDX12_Shutdown();
	
	SafeRelease(context->command_list);
	for (auto& command_allocator : context->command_allocators) SafeRelease(command_allocator);
	SafeRelease(context->frame_sync_fence);
	SafeRelease(context->rtv_descriptor_heap.d3d12);
	SafeRelease(context->resource_descriptor_heap.d3d12);
	SafeRelease(context->graphics_command_queue.d3d12);
	SafeRelease(context->device.d3d12);
}

struct SwapChainBackBuffer {
	ID3D12Resource* resource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
};

struct WindowSwapChainD3D12 : WindowSwapChain {
	IDXGISwapChain3* dxgi_swap_chain = nullptr;
	
	SwapChainBackBuffer back_buffers[number_of_back_buffers] = {};
};

static void CreateSwapChainBackBuffers(WindowSwapChainD3D12* swap_chain, GraphicsContextD3D12* context) {
	auto* dxgi_swap_chain = swap_chain->dxgi_swap_chain;
	auto* device = context->device.d3d12;
	auto* rtv_descriptor_heap = context->rtv_descriptor_heap.d3d12;
	
	u64 heap_base = rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart().ptr;
	u32 heap_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	
	for (u32 i = 0; i < number_of_back_buffers; i += 1) {
		auto& back_buffer = swap_chain->back_buffers[i];
		dxgi_swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffer.resource));
		back_buffer.descriptor.ptr = heap_base + heap_increment * i;
		
		device->CreateRenderTargetView(back_buffer.resource, nullptr, back_buffer.descriptor);
	}
}

static void ReleaseSwapChainBackBuffers(WindowSwapChainD3D12* swap_chain) {
	for (auto& back_buffer : swap_chain->back_buffers) {
		SafeRelease(back_buffer.resource);
	}
}

WindowSwapChain* CreateWindowSwapChain(StackAllocator* alloc, GraphicsContext* api_context, void* hwnd) {
	auto* context = (GraphicsContextD3D12*)api_context;

	auto* swap_chain = NewFromAlloc(alloc, WindowSwapChainD3D12);
	swap_chain->width  = 0;
	swap_chain->height = 0;
	
	IDXGIFactory4* dxgi_factory_4 = nullptr;
	defer{ SafeRelease(dxgi_factory_4); };
	
	if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&dxgi_factory_4)))) {
		DebugAssertAlways("CreateDXGIFactory failed.");
		return nullptr;
	}
	
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.Width       = 0;
	swap_chain_desc.Height      = 0;
	swap_chain_desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.Stereo      = false;
	swap_chain_desc.SampleDesc  = { 1, 0 };
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount = number_of_back_buffers;
	swap_chain_desc.Scaling     = DXGI_SCALING_NONE;
	swap_chain_desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
	swap_chain_desc.Flags       = 0;
	
	IDXGISwapChain1* dxgi_swap_chain_1 = nullptr;
	if (FAILED(dxgi_factory_4->CreateSwapChainForHwnd(context->graphics_command_queue.d3d12, (HWND)hwnd, &swap_chain_desc, nullptr, nullptr, &dxgi_swap_chain_1))) {
		DebugAssertAlways("CreateSwapChainForHwnd failed.");
		return nullptr;
	}
	
	if (FAILED(dxgi_swap_chain_1->QueryInterface(IID_PPV_ARGS(&swap_chain->dxgi_swap_chain)))) {
		DebugAssertAlways("SwapChain QueryInterface failed.");
		return nullptr;
	}
	dxgi_swap_chain_1->Release();
	
	CreateSwapChainBackBuffers(swap_chain, context);
	
	return swap_chain;
}

void ReleaseWindowSwapChain(WindowSwapChain* api_swap_chain, GraphicsContext* api_context) {
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	auto* context = (GraphicsContextD3D12*)api_context;
	
	WaitForLastFrame(context);
	ReleaseSwapChainBackBuffers(swap_chain);
	
	SafeRelease(swap_chain->dxgi_swap_chain);
}

void ResizeWindowSwapChain(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, u32 width, u32 height) {
	if (api_swap_chain->width == width && api_swap_chain->height == height) return;
	
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForLastFrame(context);
	
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	ReleaseSwapChainBackBuffers(swap_chain);
	
	if (FAILED(swap_chain->dxgi_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
		DebugAssertAlways("ResizeBuffers failed.");
		return;
	}
	
	swap_chain->width  = width;
	swap_chain->height = height;
	
	CreateSwapChainBackBuffers(swap_chain, context);
}

void WindowSwapChainBeginFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForNextFrame(context);
	
	auto* command_allocator = context->command_allocators[context->frame_index % number_of_frames_in_flight];
	auto* command_list = context->command_list;
	
	command_allocator->Reset();
	command_list->Reset(command_allocator, nullptr);
	command_list->SetDescriptorHeaps(1, &context->resource_descriptor_heap.d3d12);
	
	ImGui_ImplDX12_NewFrame();
}

void WindowSwapChainEndFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context) {
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto& back_buffer = swap_chain->back_buffers[swap_chain->dxgi_swap_chain->GetCurrentBackBufferIndex()];
	
	D3D12_TEXTURE_BARRIER barrier = {};
	barrier.SyncBefore   = D3D12_BARRIER_SYNC_NONE;
	barrier.SyncAfter    = D3D12_BARRIER_SYNC_RENDER_TARGET;
	barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
	barrier.AccessAfter  = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
	barrier.LayoutAfter  = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	barrier.pResource    = back_buffer.resource;
	barrier.Subresources = {};
	barrier.Flags        = D3D12_TEXTURE_BARRIER_FLAG_NONE;
	
	D3D12_BARRIER_GROUP barrier_group = {};
	barrier_group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
	barrier_group.NumBarriers      = 1;
	barrier_group.pTextureBarriers = &barrier;
	
	auto* command_list = context->command_list;
	command_list->Barrier(1, &barrier_group);
	
	float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
	command_list->ClearRenderTargetView(back_buffer.descriptor, clear_color, 0, nullptr);
	command_list->OMSetRenderTargets(1, &back_buffer.descriptor, false, nullptr);
	
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context->command_list);
	
	barrier.SyncBefore   = D3D12_BARRIER_SYNC_RENDER_TARGET;
	barrier.SyncAfter    = D3D12_BARRIER_SYNC_NONE;
	barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	barrier.AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS;
	barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	barrier.LayoutAfter  = D3D12_BARRIER_LAYOUT_COMMON;
	command_list->Barrier(1, &barrier_group);
	
	command_list->Close();
	
	auto* command_queue = context->graphics_command_queue.d3d12;
	command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_list);
	
	if (FAILED(swap_chain->dxgi_swap_chain->Present(0, 0))) {
		DebugAssertAlways("Present failed.");
	}
	command_queue->Signal(context->frame_sync_fence, context->frame_index);
	
	context->frame_index += 1;
}

