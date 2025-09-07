#pragma once
#include "Basic/Basic.h"

compile_const u32 number_of_frames_in_flight = 2;
compile_const u32 number_of_back_buffers     = 3;

union NativeDevice {
	struct ID3D12Device4* d3d12 = nullptr;
};

union NativeCommandQueue {
	struct ID3D12CommandQueue* d3d12 = nullptr;
};

union NativeDescriptorHeap {
	struct ID3D12DescriptorHeap* d3d12 = nullptr;
};

struct GraphicsContext {
	NativeDevice device;
	NativeCommandQueue graphics_command_queue;
	NativeDescriptorHeap resource_descriptor_heap;
	NativeDescriptorHeap rtv_descriptor_heap;
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
void WindowSwapChainBeginFrame(WindowSwapChain* swap_chain, GraphicsContext* context);
void WindowSwapChainEndFrame(WindowSwapChain* swap_chain, GraphicsContext* context);
