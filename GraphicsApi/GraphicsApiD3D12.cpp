#include "GraphicsApiD3D12.h"
#include "RecordContext.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Engine/ShaderCompiler.h"
#include "Engine/RenderPasses.h"

#include <SDK/imgui/backends/imgui_impl_dx12.h>

extern "C" __declspec(dllexport) extern const UINT  D3D12SDKVersion = 618;
extern "C" __declspec(dllexport) extern const char* D3D12SDKPath    = u8".\\D3D12\\";

static struct {
	FixedCountArray<ID3D12PipelineState*, 3> pipeline_state = {};
} debug_draw_triangle;

static ShaderCompiler* shader_compiler = nullptr;

static void WaitForLastFrame(GraphicsContextD3D12* context) {
	if (context->frame_index <= 1) return;
	context->frame_sync_fence->SetEventOnCompletion(context->frame_index - 1, nullptr);
}

static void WaitForNextFrame(GraphicsContextD3D12* context) {
	if (context->frame_index <= number_of_frames_in_flight) return;
	context->frame_sync_fence->SetEventOnCompletion(context->frame_index - number_of_frames_in_flight, nullptr);
}

static void CreateTestPipelines(GraphicsContextD3D12* api_context, StackAllocator* alloc);
static void BuildPipelineStates(GraphicsContextD3D12* context, StackAllocator* alloc);

GraphicsContext* CreateGraphicsContext(StackAllocator* alloc) {
	auto* context = NewFromAlloc(alloc, GraphicsContextD3D12);
	
	ID3D12Debug* debug = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
		debug->EnableDebugLayer();
	}
	
	ID3D12Device10* device = nullptr;
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)))) {
		DebugAssertAlways("D3D12CreateDevice failed.");
		return nullptr;
	}
	context->device = device;
	
	if (debug != nullptr) {
		ID3D12InfoQueue* info_queue = nullptr;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
			info_queue->Release();
		}
		debug->Release();
	}
	
	
	for (u32 i = 0; i < (u32)DescriptorHeapType::Count; i += 1) {
		compile_const D3D12_DESCRIPTOR_HEAP_TYPE heap_type_map[(u32)DescriptorHeapType::Count] = {
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		};
		
		compile_const u32 descriptor_heap_size_map[(u32)DescriptorHeapType::Count] = {
			persistent_srv_descriptor_count + transient_srv_descriptor_count,
			rtv_descriptor_count,
			dsv_descriptor_count,
		};
		
		bool is_shader_visible_heap = (i == (u32)DescriptorHeapType::SRV);
		
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type           = heap_type_map[i];
		heap_desc.NumDescriptors = descriptor_heap_size_map[i];
		heap_desc.Flags          = is_shader_visible_heap ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heap_desc.NodeMask       = 0;
		
		ID3D12DescriptorHeap* descriptor_heap = nullptr;
		if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&descriptor_heap)))) {
			DebugAssertAlways("CreateDescriptorHeap failed.");
			return nullptr;
		}
		
		context->descriptor_heaps[i] = descriptor_heap;
		context->descriptor_sizes[i] = device->GetDescriptorHandleIncrementSize(heap_desc.Type);
		context->cpu_base_handles[i] = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
		context->gpu_base_handles[i] = is_shader_visible_heap ? descriptor_heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };
	}
	
	
	{
		ArrayResize(context->srv_heap_free_indices, alloc, persistent_srv_descriptor_count);
		static_assert(persistent_srv_descriptor_count - 1 <= u16_max, "Persistent SRV indices are too large.");
		
		for (u32 i = 0; i < persistent_srv_descriptor_count; i += 1) {
			context->srv_heap_free_indices[i] = (u16)i;
		}
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
		if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
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
		init_info.RTVFormat         = DXGI_FORMAT_R8G8B8A8_UNORM;
		init_info.DSVFormat         = DXGI_FORMAT_UNKNOWN;
		init_info.UserData          = context;
		init_info.SrvDescriptorHeap = context->descriptor_heaps[(u32)DescriptorHeapType::SRV];
		
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
			auto* context = (GraphicsContextD3D12*)init_info->UserData;
			
			DebugAssert(context->srv_heap_free_indices.count != 0, "Resource descriptor heap is exhausted.");
			u64 index = ArrayPopLast(context->srv_heap_free_indices);
			
			auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::SRV].ptr;
			auto gpu_base_handle = context->gpu_base_handles[(u32)DescriptorHeapType::SRV].ptr;
			auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
			
			out_cpu_desc_handle->ptr = cpu_base_handle + index * descriptor_size;
			out_gpu_desc_handle->ptr = gpu_base_handle + index * descriptor_size;
		};
		
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE) {
			auto* context = (GraphicsContextD3D12*)init_info->UserData;
			
			auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::SRV].ptr;
			auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
			
			u64 index = (cpu_desc_handle.ptr - cpu_base_handle) / descriptor_size;
			DebugAssert(index < persistent_srv_descriptor_count, "Deallocated SRV index is out of bounds.");
			
			ArrayAppend(context->srv_heap_free_indices, (u16)index);
		};
		ImGui_ImplDX12_Init(&init_info);
	}
	
	{
		shader_compiler = CreateShaderCompiler(alloc);
		
		auto filepath = "./Build/RootSignature.bin"_sl;
		auto file = SystemReadFileToString(alloc, filepath);
		DebugAssert(file.data != nullptr, "Failed to read root signature file '%.*s'.", (s32)filepath.count, filepath.data);
		
		ArrayView<u32> offset_table;
		offset_table.count = *(u32*)file.data;
		offset_table.data  = (u32*)file.data + 1;
		
		ArrayResize(context->root_signature_table, alloc, offset_table.count);
		for (u32 i = 0; i < offset_table.count; i += 1) {
			u32 offset = offset_table[i];
			u32 size   = (u32)(i + 1 >= offset_table.count ? file.count : offset_table[i + 1]) - offset;
			
			ID3D12RootSignature* root_signature = nullptr;
			if (FAILED(device->CreateRootSignature(0, file.data + offset, size, IID_PPV_ARGS(&root_signature)))) {
				DebugAssertAlways("Failed to create root signature.");
				return nullptr;
			}
			
			context->root_signature_table[i] = root_signature;
		}
		
		CreateTestPipelines(context, alloc);
		
		extern Array<PipelineDefinition> GatherPipelineDefinitions(StackAllocator* alloc);
		context->pipeline_definitions = GatherPipelineDefinitions(alloc);
		ArrayResize(context->pipeline_state_table, alloc, context->pipeline_definitions.count);
		
		BuildPipelineStates(context, alloc);
	}
	
	return context;
}

static void CreateTestPipelines(GraphicsContextD3D12* context, StackAllocator* alloc) {
	auto* device = context->device;
	
	extern ArrayView<ShaderDefinition*> shader_definition_table;
	auto* shader = shader_definition_table[DrawTriangleShadersID.index];
	
	auto bytecode      = CompileShader(shader_compiler, alloc, shader, (u64)DrawTriangleShaders::None, ShaderTypeMask::PixelShader | ShaderTypeMask::VertexShader, "Basic.hlsl"_sl);
	auto bytecode_red  = CompileShader(shader_compiler, alloc, shader, (u64)DrawTriangleShaders::RedColor, ShaderTypeMask::PixelShader | ShaderTypeMask::VertexShader, "Basic.hlsl"_sl);
	auto bytecode_blue = CompileShader(shader_compiler, alloc, shader, (u64)DrawTriangleShaders::BlueColor, ShaderTypeMask::PixelShader | ShaderTypeMask::VertexShader, "Basic.hlsl"_sl);
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature                            = context->root_signature_table[DrawTriangleRenderPass::root_signature.root_signature_index];
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
	desc.RTVFormats[0]                             = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.DSVFormat                                 = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc                                = { 1, 0 };
	desc.NodeMask                                  = 0;
	desc.CachedPSO                                 = {};
	desc.Flags                                     = D3D12_PIPELINE_STATE_FLAG_NONE;
	
	
	ID3D12PipelineState* pipeline_state = nullptr;
	
	desc.VS.pShaderBytecode = bytecode[(u32)ShaderType::VertexShader].data;
	desc.VS.BytecodeLength  = bytecode[(u32)ShaderType::VertexShader].count;
	desc.PS.pShaderBytecode = bytecode[(u32)ShaderType::PixelShader].data;
	desc.PS.BytecodeLength  = bytecode[(u32)ShaderType::PixelShader].count;
	if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)))) {
		DebugAssertAlways("Failed to create graphics pipeline state.");
		return;
	}
	SafeRelease(debug_draw_triangle.pipeline_state[0]);
	debug_draw_triangle.pipeline_state[0] = pipeline_state;
	
	desc.VS.pShaderBytecode = bytecode_red[(u32)ShaderType::VertexShader].data;
	desc.VS.BytecodeLength  = bytecode_red[(u32)ShaderType::VertexShader].count;
	desc.PS.pShaderBytecode = bytecode_red[(u32)ShaderType::PixelShader].data;
	desc.PS.BytecodeLength  = bytecode_red[(u32)ShaderType::PixelShader].count;
	if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)))) {
		DebugAssertAlways("Failed to create graphics pipeline state.");
		return;
	}
	SafeRelease(debug_draw_triangle.pipeline_state[1]);
	debug_draw_triangle.pipeline_state[1] = pipeline_state;
	
	desc.VS.pShaderBytecode = bytecode_blue[(u32)ShaderType::VertexShader].data;
	desc.VS.BytecodeLength  = bytecode_blue[(u32)ShaderType::VertexShader].count;
	desc.PS.pShaderBytecode = bytecode_blue[(u32)ShaderType::PixelShader].data;
	desc.PS.BytecodeLength  = bytecode_blue[(u32)ShaderType::PixelShader].count;
	if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)))) {
		DebugAssertAlways("Failed to create graphics pipeline state.");
		return;
	}
	SafeRelease(debug_draw_triangle.pipeline_state[2]);
	debug_draw_triangle.pipeline_state[2] = pipeline_state;
}

static void CreateComputePipelineState(GraphicsContextD3D12* context, const ShaderBytecode& bytecode, u32 root_signature_index, ID3D12PipelineState*& pipeline_state) {
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	desc.NodeMask  = 0;
	desc.CachedPSO = {};
	desc.Flags     = D3D12_PIPELINE_STATE_FLAG_NONE;
	
	desc.pRootSignature     = context->root_signature_table[root_signature_index];
	desc.CS.pShaderBytecode = bytecode[(u32)ShaderType::ComputeShader].data;
	desc.CS.BytecodeLength  = bytecode[(u32)ShaderType::ComputeShader].count;
	
	ID3D12PipelineState* new_pipeline_state = nullptr;
	if (FAILED(context->device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&new_pipeline_state)))) {
		DebugAssertAlways("Failed to create compute pipeline state.");
		return;
	}
	
	SafeRelease(pipeline_state);
	pipeline_state = new_pipeline_state;
}

static void BuildPipelineStates(GraphicsContextD3D12* context, StackAllocator* alloc) {
	extern String root_signature_include_filenames[];
	
	for (u64 i = 0; i < context->pipeline_definitions.count; i += 1) {
		auto& definition = context->pipeline_definitions[i];
		
		auto bytecode = CompileShader(shader_compiler, alloc, definition.shader_definition, definition.permutation, definition.shader_type_mask, root_signature_include_filenames[definition.root_signature_index]);
		CreateComputePipelineState(context, bytecode, definition.root_signature_index, context->pipeline_state_table[i]);
	}
}


void ReleaseGraphicsContext(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	ImGui_ImplDX12_Shutdown();
	
	ReleaseShaderCompiler(shader_compiler);
	
	SafeRelease(context->command_list);
	for (auto& command_allocator : context->command_allocators) SafeRelease(command_allocator);
	SafeRelease(context->frame_sync_fence);
	for (auto& descriptor_heap : context->descriptor_heaps) SafeRelease(descriptor_heap);
	SafeRelease(context->graphics_command_queue);
	SafeRelease(context->device);
}

static NativeTextureResource CreateTextureResource(GraphicsContext* api_context, TextureSize size) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
	heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_properties.CreationNodeMask     = 0;
	heap_properties.VisibleNodeMask      = 0;
	
	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension        = size.type == TextureSize::Type::Texture3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource_desc.Alignment        = 0;
	resource_desc.Width            = size.x;
	resource_desc.Height           = size.y;
	resource_desc.DepthOrArraySize = size.z;
	resource_desc.MipLevels        = size.mips;
	resource_desc.Format           = dxgi_texture_format_map[(u32)size.format];
	resource_desc.SampleDesc       = { 1, 0 };
	resource_desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resource_desc.SamplerFeedbackMipRegion = { 0, 0, 0 };
	
	NativeTextureResource resource = {};
	auto result = context->device->CreateCommittedResource3( 
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&resource_desc,
		D3D12_BARRIER_LAYOUT_COMMON,
		nullptr,
		nullptr,
		0,
		nullptr,
		IID_PPV_ARGS(&resource.d3d12)
	);
	DebugAssert(SUCCEEDED(result), "Failed to create texture resource.");
	
	return resource;
}

u32 AllocateTransientSrvDescriptorTable(GraphicsContext* api_context, u32 count) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	u32 offset = context->srv_heap_offset;
	if (offset + count > transient_srv_descriptor_count) offset = 0;
	
	context->srv_heap_offset = offset + count;
	
	return offset + persistent_srv_descriptor_count;
}


static void CreateSwapChainBackBuffers(WindowSwapChainD3D12* swap_chain, GraphicsContextD3D12* context) {
	auto* dxgi_swap_chain = swap_chain->dxgi_swap_chain;
	auto* device = context->device;
	
	auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::RTV].ptr;
	auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::RTV];
	
	for (u32 index = 0; index < number_of_back_buffers; index += 1) {
		auto& back_buffer = swap_chain->back_buffers[index];
		dxgi_swap_chain->GetBuffer(index, IID_PPV_ARGS(&back_buffer.resource));
		back_buffer.descriptor.ptr = cpu_base_handle + index * descriptor_size;
		
		D3D12_RENDER_TARGET_VIEW_DESC desc = {};
		desc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice   = 0;
		desc.Texture2D.PlaneSlice = 0;
		device->CreateRenderTargetView(back_buffer.resource, &desc, back_buffer.descriptor);
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
	swap_chain->size = 0;
	
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

void ResizeWindowSwapChain(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, uint2 size) {
	if (api_swap_chain->size.x == size.x && api_swap_chain->size.y == size.y) return;
	
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForLastFrame(context);
	
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	ReleaseSwapChainBackBuffers(swap_chain);
	
	if (FAILED(swap_chain->dxgi_swap_chain->ResizeBuffers(0, size.x, size.y, DXGI_FORMAT_UNKNOWN, 0))) {
		DebugAssertAlways("ResizeBuffers failed.");
		return;
	}
	
	swap_chain->size = size;
	
	CreateSwapChainBackBuffers(swap_chain, context);
}

void WindowSwapChainBeginFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc) {
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForNextFrame(context);
	
	if (CheckShaderFileChanges(shader_compiler, alloc)) {
		WaitForLastFrame(context);
		CreateTestPipelines(context, alloc);
		BuildPipelineStates(context, alloc);
	}
	
	auto* command_allocator = context->command_allocators[context->frame_index % number_of_frames_in_flight];
	auto* command_list = context->command_list;
	
	command_allocator->Reset();
	command_list->Reset(command_allocator, nullptr);
	command_list->SetDescriptorHeaps(1, &context->descriptor_heaps[(u32)DescriptorHeapType::SRV]);
	
	ImGui_ImplDX12_NewFrame();
}

void WindowSwapChainEndFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc, RecordContext& record_context) {
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
	
	// command_list->SetGraphicsRootSignature(context->root_signature_table[DrawTriangleRenderPass::root_signature.root_signature_index]);
	// command_list->SetPipelineState(debug_draw_triangle.pipeline_state[(context->frame_index / 128) % 3]);
	
	auto& resource_table = record_context.resource_table->virtual_resources;
	for (auto& resource : resource_table) {
		if (resource.type == VirtualResource::Type::VirtualTexture && resource.texture.size != resource.texture.allocated_size) {
			resource.texture.resource = CreateTextureResource(context, resource.texture.size);
			resource.texture.allocated_size = resource.texture.size;
		}
	}
	
	struct ImGuiDescriptorTable : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float4> transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float4> multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
		HLSL::Texture2D<float4> sky_panorama_lut        = VirtualResourceID::SkyPanoramaLut;
	};
	HLSL::DescriptorTable<ImGuiDescriptorTable> root_descriptor_table = { 0, 3 };
	
	auto& descriptor_table = AllocateDescriptorTable(&record_context, root_descriptor_table);
	
	// CmdClearRenderTarget(&record_context, back_buffer.descriptor.ptr);
	// CmdSetRenderTargets(&record_context, { &back_buffer.descriptor.ptr, 1 });
	// CmdSetViewportAndScissor(&record_context, swap_chain->size);
	// CmdDrawInstanced(&record_context, 3);
	
	ReplayRecordContext(context, &record_context);
	
	float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
	command_list->ClearRenderTargetView(back_buffer.descriptor, clear_color, 0, nullptr);
	command_list->OMSetRenderTargets(1, &back_buffer.descriptor, false, nullptr);
	
	auto gpu_base_handle = context->gpu_base_handles[(u32)DescriptorHeapType::SRV];
	auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
	
	{
		ImGui::Begin("Transmittance LUT");
		auto size = resource_table[(u32)VirtualResourceID::TransmittanceLut].texture.size;
		ImGui::Image(gpu_base_handle.ptr + (descriptor_table.descriptor_heap_offset + 0) * descriptor_size, ImVec2(size.x, size.y));
		ImGui::End();
	}
	
	{
		ImGui::Begin("Multiple Scattering LUT");
		auto size = resource_table[(u32)VirtualResourceID::MultipleScatteringLut].texture.size;
		ImGui::Image(gpu_base_handle.ptr + (descriptor_table.descriptor_heap_offset + 1) * descriptor_size, ImVec2(size.x, size.y));
		ImGui::End();
	}
	
	{
		ImGui::Begin("Sky Panorma LUT");
		auto size = resource_table[(u32)VirtualResourceID::SkyPanoramaLut].texture.size;
		ImGui::Image(gpu_base_handle.ptr + (descriptor_table.descriptor_heap_offset + 2) * descriptor_size, ImVec2(size.x, size.y) * 4.f);
		ImGui::End();
	}
	
	ImGui::Render();
	
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

