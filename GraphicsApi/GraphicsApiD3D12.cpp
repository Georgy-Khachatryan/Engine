#include "GraphicsApiD3D12.h"
#include "RecordContext.h"
#include "Basic/BasicMemory.h"
#include "Engine/ShaderCompiler.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <SDK/imgui/backends/imgui_impl_dx12.h>

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
	context->device = device;
	
	
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
		context->resource_descriptor_heap = resource_descriptor_heap;
		
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
		context->rtv_descriptor_heap = rtv_descriptor_heap;
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
		context->graphics_command_queue = queue;
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
		init_info.CommandQueue      = context->graphics_command_queue;
		init_info.NumFramesInFlight = number_of_frames_in_flight;
		init_info.RTVFormat         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		init_info.DSVFormat         = DXGI_FORMAT_UNKNOWN;
		init_info.UserData          = context;
		init_info.SrvDescriptorHeap = context->resource_descriptor_heap;
		
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
			auto* context = (GraphicsContextD3D12*)init_info->UserData;
			
			DebugAssert(context->free_index_count != 0, "Resource descriptor heap is exhausted.");
			u32 index = context->free_indices[--context->free_index_count];
			
			auto* resource_descriptor_heap = context->resource_descriptor_heap;
			u64 cpu_heap_base = resource_descriptor_heap->GetCPUDescriptorHandleForHeapStart().ptr;
			u64 gpu_heap_base = resource_descriptor_heap->GetGPUDescriptorHandleForHeapStart().ptr;
			u32 heap_increment = context->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			
			out_cpu_desc_handle->ptr = cpu_heap_base + index * heap_increment;
			out_gpu_desc_handle->ptr = gpu_heap_base + index * heap_increment;
		};
		
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE) {
			auto* context = (GraphicsContextD3D12*)init_info->UserData;
			
			auto* resource_descriptor_heap = context->resource_descriptor_heap;
			u64 cpu_heap_base  = resource_descriptor_heap->GetCPUDescriptorHandleForHeapStart().ptr;
			u32 heap_increment = context->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			
			u32 index = (u32)((cpu_desc_handle.ptr - cpu_heap_base) / heap_increment);
			context->free_indices[context->free_index_count++] = index;
		};
		ImGui_ImplDX12_Init(&init_info);
	}
	
	{
		TempAllocationScope(alloc);
		
		auto* shader_compiler = CreateShaderCompiler(alloc);
		defer{ ReleaseShaderCompiler(shader_compiler); };
		
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs_desc = {};
		rs_desc.Version  = D3D_ROOT_SIGNATURE_VERSION_1_2;
		rs_desc.Desc_1_2 = {};
		
		ID3DBlob* rs_blob = nullptr;
		if (FAILED(D3D12SerializeVersionedRootSignature(&rs_desc, &rs_blob, nullptr))) {
			DebugAssertAlways("Failed to serialize root signature.");
			return nullptr;
		}
		defer{ SafeRelease(rs_blob); };
		
		ID3D12RootSignature* root_signature = nullptr;
		if (FAILED(device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)))) {
			DebugAssertAlways("Failed to create root signature.");
			return nullptr;
		}
		
		FixedCapacityArray<String, 2> defines;
		ArrayAppend(defines, "RED_COLOR"_sl);
		ArrayAppend(defines, "BLUE_COLOR"_sl);
		
		ShaderDefinition shader;
		shader.filename = "DrawTriangle.hlsl"_sl;
		shader.defines  = defines;
		
		auto bytecode      = CompileShader(shader_compiler, &shader, 0x0, ShaderTypeMask::PixelShader | ShaderTypeMask::VertexShader);
		auto bytecode_red  = CompileShader(shader_compiler, &shader, 0x1, ShaderTypeMask::PixelShader | ShaderTypeMask::VertexShader);
		auto bytecode_blue = CompileShader(shader_compiler, &shader, 0x2, ShaderTypeMask::PixelShader | ShaderTypeMask::VertexShader);
		
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature                            = root_signature;
		desc.BlendState.AlphaToCoverageEnable          = false;
		desc.BlendState.IndependentBlendEnable         = false;
		desc.BlendState.RenderTarget[0].BlendEnable    = true;
		desc.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.SampleMask                                = u32_max;
		desc.RasterizerState.FillMode                  = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode                  = D3D12_CULL_MODE_BACK;
		desc.RasterizerState.FrontCounterClockwise     = true;
		desc.RasterizerState.DepthBias                 = 0;
		desc.RasterizerState.DepthBiasClamp            = 0.f;
		desc.RasterizerState.SlopeScaledDepthBias      = 0.f;
		desc.RasterizerState.DepthClipEnable           = true;
		desc.RasterizerState.MultisampleEnable         = false;
		desc.RasterizerState.AntialiasedLineEnable     = false;
		desc.RasterizerState.ForcedSampleCount         = 0;
		desc.RasterizerState.ConservativeRaster        = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		desc.DepthStencilState.DepthEnable             = false;
		desc.DepthStencilState.DepthWriteMask          = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc               = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.DepthStencilState.StencilEnable           = false;
		desc.DepthStencilState.StencilReadMask         = 0;
		desc.DepthStencilState.StencilWriteMask        = 0;
		desc.DepthStencilState.FrontFace               = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
		desc.DepthStencilState.BackFace                = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
		desc.InputLayout                               = {};
		desc.IBStripCutValue                           = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		desc.PrimitiveTopologyType                     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets                          = 1;
		desc.RTVFormats[0]                             = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.DSVFormat                                 = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc                                = { 1, 0 };
		desc.NodeMask                                  = 0;
		desc.CachedPSO                                 = {};
		desc.Flags                                     = D3D12_PIPELINE_STATE_FLAG_NONE;
		
		context->root_signature = root_signature;
		
		ID3D12PipelineState* pipeline_state = nullptr;
		
		desc.VS.pShaderBytecode = bytecode[(u32)ShaderType::VertexShader].data;
		desc.VS.BytecodeLength  = bytecode[(u32)ShaderType::VertexShader].count;
		desc.PS.pShaderBytecode = bytecode[(u32)ShaderType::PixelShader].data;
		desc.PS.BytecodeLength  = bytecode[(u32)ShaderType::PixelShader].count;
		if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)))) {
			DebugAssertAlways("Failed to create graphics pipeline state.");
			return nullptr;
		}
		context->pipeline_state[0] = pipeline_state;
		
		desc.VS.pShaderBytecode = bytecode_red[(u32)ShaderType::VertexShader].data;
		desc.VS.BytecodeLength  = bytecode_red[(u32)ShaderType::VertexShader].count;
		desc.PS.pShaderBytecode = bytecode_red[(u32)ShaderType::PixelShader].data;
		desc.PS.BytecodeLength  = bytecode_red[(u32)ShaderType::PixelShader].count;
		if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)))) {
			DebugAssertAlways("Failed to create graphics pipeline state.");
			return nullptr;
		}
		context->pipeline_state[1] = pipeline_state;
		
		desc.VS.pShaderBytecode = bytecode_blue[(u32)ShaderType::VertexShader].data;
		desc.VS.BytecodeLength  = bytecode_blue[(u32)ShaderType::VertexShader].count;
		desc.PS.pShaderBytecode = bytecode_blue[(u32)ShaderType::PixelShader].data;
		desc.PS.BytecodeLength  = bytecode_blue[(u32)ShaderType::PixelShader].count;
		if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)))) {
			DebugAssertAlways("Failed to create graphics pipeline state.");
			return nullptr;
		}
		context->pipeline_state[2] = pipeline_state;
	}
	
	return context;
}

void ReleaseGraphicsContext(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;

	ImGui_ImplDX12_Shutdown();
	
	SafeRelease(context->command_list);
	for (auto& command_allocator : context->command_allocators) SafeRelease(command_allocator);
	SafeRelease(context->frame_sync_fence);
	SafeRelease(context->rtv_descriptor_heap);
	SafeRelease(context->resource_descriptor_heap);
	SafeRelease(context->graphics_command_queue);
	SafeRelease(context->device);
}


static void CreateSwapChainBackBuffers(WindowSwapChainD3D12* swap_chain, GraphicsContextD3D12* context) {
	auto* dxgi_swap_chain = swap_chain->dxgi_swap_chain;
	auto* device = context->device;
	auto* rtv_descriptor_heap = context->rtv_descriptor_heap;
	
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
	if (FAILED(dxgi_factory_4->CreateSwapChainForHwnd(context->graphics_command_queue, (HWND)hwnd, &swap_chain_desc, nullptr, nullptr, &dxgi_swap_chain_1))) {
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
	command_list->SetDescriptorHeaps(1, &context->resource_descriptor_heap);
	
	ImGui_ImplDX12_NewFrame();
}

void WindowSwapChainEndFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc) {
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
	
	RecordContext record_context;
	record_context.alloc = alloc;
	
	command_list->SetGraphicsRootSignature(context->root_signature);
	command_list->SetPipelineState(context->pipeline_state[(context->frame_index / 128) % 3]);
	
	CmdClearRenderTarget(&record_context, back_buffer.descriptor.ptr);
	CmdSetRenderTargets(&record_context, { &back_buffer.descriptor.ptr, 1 });
	CmdSetViewportAndScissor(&record_context, swap_chain->width, swap_chain->height);
	CmdDrawInstanced(&record_context, 3);
	ReplayRecordContext(context, &record_context);
	
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context->command_list);
	
	barrier.SyncBefore   = D3D12_BARRIER_SYNC_RENDER_TARGET;
	barrier.SyncAfter    = D3D12_BARRIER_SYNC_NONE;
	barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	barrier.AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS;
	barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	barrier.LayoutAfter  = D3D12_BARRIER_LAYOUT_COMMON;
	command_list->Barrier(1, &barrier_group);
	
	command_list->Close();
	
	auto* command_queue = context->graphics_command_queue;
	command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_list);
	
	u32 sync_interval = 1;
	if (FAILED(swap_chain->dxgi_swap_chain->Present(sync_interval, 0))) {
		DebugAssertAlways("Present failed.");
	}
	command_queue->Signal(context->frame_sync_fence, context->frame_index);
	
	context->frame_index += 1;
}

