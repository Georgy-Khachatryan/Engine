#include "GraphicsApiD3D12.h"
#include "RecordContext.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Engine/ShaderCompiler.h"
#include "Engine/RenderPasses.h"

extern "C" __declspec(dllexport) extern const UINT  D3D12SDKVersion = 618;
extern "C" __declspec(dllexport) extern const char* D3D12SDKPath    = u8".\\D3D12\\";

static ShaderCompiler* shader_compiler = nullptr;

void WaitForLastFrame(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	if (context->frame_index <= 1) return;
	context->frame_sync_fence->SetEventOnCompletion(context->frame_index - 1, nullptr);
}

void WaitForNextFrame(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	if (context->frame_index <= number_of_frames_in_flight) return;
	context->frame_sync_fence->SetEventOnCompletion(context->frame_index - number_of_frames_in_flight, nullptr);
}

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
		
		extern Array<PipelineDefinition> GatherPipelineDefinitions(StackAllocator* alloc);
		context->pipeline_definitions = GatherPipelineDefinitions(alloc);
		ArrayResize(context->pipeline_state_table, alloc, context->pipeline_definitions.count);
		
		BuildPipelineStates(context, alloc);
	}
	
	return context;
}

template<typename T>
struct alignas(void*) PipelineSubobjectD3D12 {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = {};
	T data = {};
};

template<typename T, u64 fixed_stream_capacity>
static T& AppendSubobject(FixedCapacityArray<u8, fixed_stream_capacity>& stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) {
	u64 subobject_size = sizeof(PipelineSubobjectD3D12<T>);
	DebugAssert(stream.count + subobject_size <= stream.capacity, "Subobject stream overflow.");
	
	auto* subobject = NewInPlace(stream.data + stream.count, PipelineSubobjectD3D12<T>);
	stream.count += subobject_size;
	subobject->type = type;
	
	return subobject->data;
}

static void CreateComputePipelineState(GraphicsContextD3D12* context, const ShaderBytecode& bytecode, u32 root_signature_index, u32 pipeline_state_index) {
	FixedCapacityArray<u8, 40> stream;
	
	auto& root_signature = AppendSubobject<ID3D12RootSignature*>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE);
	root_signature = context->root_signature_table[root_signature_index];
	
	auto& cs = AppendSubobject<D3D12_SHADER_BYTECODE>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS);
	cs.pShaderBytecode = bytecode[(u32)ShaderType::ComputeShader].data;
	cs.BytecodeLength  = bytecode[(u32)ShaderType::ComputeShader].count;
	
	auto desc = D3D12_PIPELINE_STATE_STREAM_DESC{ stream.count, stream.data };
	
	ID3D12PipelineState* new_pipeline_state = nullptr;
	if (FAILED(context->device->CreatePipelineState(&desc, IID_PPV_ARGS(&new_pipeline_state)))) {
		DebugAssertAlways("Failed to create compute pipeline state.");
		return;
	}
	
	SafeRelease(context->pipeline_state_table[pipeline_state_index]);
	context->pipeline_state_table[pipeline_state_index] = new_pipeline_state;
}

static D3D12_CULL_MODE cull_mode_map[(u32)PipelineRasterizer::CullMode::Count] = {
	D3D12_CULL_MODE_NONE,
	D3D12_CULL_MODE_FRONT,
	D3D12_CULL_MODE_BACK
};

static D3D12_BLEND blend_map[(u32)PipelineBlendState::Blend::Count] = {
	D3D12_BLEND_ZERO,
	D3D12_BLEND_ONE,
	D3D12_BLEND_SRC_ALPHA,
	D3D12_BLEND_INV_SRC_ALPHA,
	D3D12_BLEND_DEST_ALPHA,
	D3D12_BLEND_INV_DEST_ALPHA,
};

static D3D12_BLEND_OP blend_op_map[(u32)PipelineBlendState::BlendOp::Count] = {
	D3D12_BLEND_OP_ADD,
	D3D12_BLEND_OP_SUBTRACT,
	D3D12_BLEND_OP_REV_SUBTRACT,
	D3D12_BLEND_OP_MIN,
	D3D12_BLEND_OP_MAX,
};

static D3D12_COMPARISON_FUNC comparison_function_map[(u32)PipelineDepthStencil::ComparisonMode::Count] = {
	D3D12_COMPARISON_FUNC_NONE,
	D3D12_COMPARISON_FUNC_ALWAYS,
	D3D12_COMPARISON_FUNC_NEVER,
	D3D12_COMPARISON_FUNC_GREATER,
	D3D12_COMPARISON_FUNC_LESS,
	D3D12_COMPARISON_FUNC_GREATER_EQUAL,
	D3D12_COMPARISON_FUNC_LESS_EQUAL,
	D3D12_COMPARISON_FUNC_EQUAL,
	D3D12_COMPARISON_FUNC_NOT_EQUAL,
};

static D3D12_STENCIL_OP stencil_op_map[(u32)PipelineDepthStencil::StencilOp::Count] = {
	D3D12_STENCIL_OP_KEEP,
	D3D12_STENCIL_OP_ZERO,
	D3D12_STENCIL_OP_REPLACE,
	D3D12_STENCIL_OP_INVERT,
	D3D12_STENCIL_OP_INCR,
	D3D12_STENCIL_OP_DECR,
	D3D12_STENCIL_OP_INCR_SAT,
	D3D12_STENCIL_OP_DECR_SAT,
};

static D3D12_DEPTH_STENCILOP_DESC TranslateStencilFaceOps(PipelineDepthStencil::StencilFaceOps ops) {
	D3D12_DEPTH_STENCILOP_DESC desc = {};
	desc.StencilFailOp      = stencil_op_map[(u32)ops.stencil_fail_depth_none];
	desc.StencilDepthFailOp = stencil_op_map[(u32)ops.stencil_pass_depth_fail];
	desc.StencilPassOp      = stencil_op_map[(u32)ops.stencil_pass_depth_pass];
	desc.StencilFunc        = comparison_function_map[(u32)ops.stencil_comparison];
	return desc;
}

static void CreateGraphicsPipelineState(GraphicsContextD3D12* context, const PipelineStateDescription& pipeline_state_description, const ShaderBytecode& bytecode, u32 root_signature_index, u32 pipeline_state_index) {
	FixedCapacityArray<u8, 560> stream;
	
	auto& root_signature = AppendSubobject<ID3D12RootSignature*>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE);
	root_signature = context->root_signature_table[root_signature_index];
	
	if (pipeline_state_description.blend_states.count != 0) {
		auto& blend_state = AppendSubobject<D3D12_BLEND_DESC>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND);
		blend_state.AlphaToCoverageEnable  = false;
		blend_state.IndependentBlendEnable = pipeline_state_description.blend_states.count != 1;
		
		for (u32 i = 0; i < pipeline_state_description.blend_states.count; i += 1) {
			auto* src_desc = pipeline_state_description.blend_states[i];
			auto& dst_desc = blend_state.RenderTarget[i];
			
			dst_desc.BlendEnable    = true;
			dst_desc.LogicOpEnable  = false;
			dst_desc.SrcBlend       = blend_map[(u32)src_desc->src_blend_rgb];
			dst_desc.DestBlend      = blend_map[(u32)src_desc->dst_blend_rgb];
			dst_desc.BlendOp        = blend_op_map[(u32)src_desc->blend_op_rgb];
			dst_desc.SrcBlendAlpha  = blend_map[(u32)src_desc->src_blend_a];
			dst_desc.DestBlendAlpha = blend_map[(u32)src_desc->dst_blend_a];
			dst_desc.BlendOpAlpha   = blend_op_map[(u32)src_desc->blend_op_a];
			dst_desc.LogicOp        = D3D12_LOGIC_OP_NOOP;
			dst_desc.RenderTargetWriteMask = (u8)src_desc->write_mask;
		}
	}
	
	auto& rasterizer_state = AppendSubobject<D3D12_RASTERIZER_DESC>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER);
	rasterizer_state.FillMode              = D3D12_FILL_MODE_SOLID;
	rasterizer_state.CullMode              = cull_mode_map[(u32)pipeline_state_description.rasterizer->cull_mode];
	rasterizer_state.FrontCounterClockwise = pipeline_state_description.rasterizer->front_face_winding == PipelineRasterizer::FrontFaceWinding::CCW;
	rasterizer_state.DepthBias             = 0;
	rasterizer_state.DepthBiasClamp        = 0.f;
	rasterizer_state.SlopeScaledDepthBias  = 0.f;
	rasterizer_state.DepthClipEnable       = true;
	rasterizer_state.MultisampleEnable     = false;
	rasterizer_state.AntialiasedLineEnable = false;
	rasterizer_state.ForcedSampleCount     = 0;
	rasterizer_state.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	
	if (pipeline_state_description.depth_stencil->format != TextureFormat::None) {
		auto* depth_stencil = pipeline_state_description.depth_stencil;
		
		auto& depth_stencil_state = AppendSubobject<D3D12_DEPTH_STENCIL_DESC>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL);
		depth_stencil_state.DepthEnable      = HasAnyFlags(depth_stencil->flags, PipelineDepthStencil::Flags::EnableDepth);
		depth_stencil_state.DepthWriteMask   = HasAnyFlags(depth_stencil->flags, PipelineDepthStencil::Flags::EnableDepthWrite) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		depth_stencil_state.DepthFunc        = comparison_function_map[(u32)depth_stencil->depth_comparison];
		depth_stencil_state.StencilEnable    = HasAnyFlags(depth_stencil->flags, PipelineDepthStencil::Flags::EnableStencil);
		depth_stencil_state.StencilReadMask  = depth_stencil->stencil_read_mask;
		depth_stencil_state.StencilWriteMask = depth_stencil->stencil_write_mask;
		depth_stencil_state.FrontFace        = TranslateStencilFaceOps(depth_stencil->front);
		depth_stencil_state.BackFace         = TranslateStencilFaceOps(depth_stencil->back);
		
		auto& depth_format = AppendSubobject<DXGI_FORMAT>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT);
		depth_format = dxgi_texture_format_map[(u32)depth_stencil->format];
	}
	
	if (bytecode[(u32)ShaderType::VertexShader].data) {
		auto& primitive_topology = AppendSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY);
		primitive_topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
	
	if (pipeline_state_description.render_targets.count != 0) {
		auto& render_targets = AppendSubobject<D3D12_RT_FORMAT_ARRAY>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS);
		render_targets.NumRenderTargets = (u32)pipeline_state_description.render_targets.count;
		for (u32 i = 0; i < pipeline_state_description.render_targets.count; i += 1) {
			render_targets.RTFormats[i] = dxgi_texture_format_map[(u32)pipeline_state_description.render_targets[i]->format];
		}
	}
	
	auto& vs = AppendSubobject<D3D12_SHADER_BYTECODE>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS);
	vs.pShaderBytecode = bytecode[(u32)ShaderType::VertexShader].data;
	vs.BytecodeLength  = bytecode[(u32)ShaderType::VertexShader].count;
	
	auto& ps = AppendSubobject<D3D12_SHADER_BYTECODE>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS);
	ps.pShaderBytecode = bytecode[(u32)ShaderType::PixelShader].data;
	ps.BytecodeLength  = bytecode[(u32)ShaderType::PixelShader].count;
	
	auto desc = D3D12_PIPELINE_STATE_STREAM_DESC{ stream.count, stream.data };
	
	ID3D12PipelineState* new_pipeline_state = nullptr;
	if (FAILED(context->device->CreatePipelineState(&desc, IID_PPV_ARGS(&new_pipeline_state)))) {
		DebugAssertAlways("Failed to create graphics pipeline state.");
		return;
	}
	
	SafeRelease(context->pipeline_state_table[pipeline_state_index]);
	context->pipeline_state_table[pipeline_state_index] = new_pipeline_state;
}

static void BuildPipelineStates(GraphicsContextD3D12* context, StackAllocator* alloc) {
	extern String root_signature_include_filenames[];
	
	for (u32 i = 0; i < context->pipeline_definitions.count; i += 1) {
		auto& definition = context->pipeline_definitions[i];
		
		auto bytecode = CompileShader(shader_compiler, alloc, definition.shader_definition, definition.permutation, definition.shader_type_mask, root_signature_include_filenames[definition.root_signature_index]);
		
		if (HasAnyFlags(definition.shader_type_mask, ShaderTypeMask::ComputeShader)) {
			CreateComputePipelineState(context, bytecode, definition.root_signature_index, i);
		} else {
			auto pipeline_state_description = CreatePipelineStateDescription(definition.pipeline_state_stream);
			CreateGraphicsPipelineState(context, pipeline_state_description, bytecode, definition.root_signature_index, i);
		}
	}
}


void ReleaseGraphicsContext(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	ReleaseShaderCompiler(shader_compiler);
	
	for (auto& root_signature : context->root_signature_table) SafeRelease(root_signature);
	for (auto& pipeline_state : context->pipeline_state_table) SafeRelease(pipeline_state);
	
	SafeRelease(context->command_list);
	for (auto& command_allocator : context->command_allocators) SafeRelease(command_allocator);
	SafeRelease(context->frame_sync_fence);
	for (auto& descriptor_heap : context->descriptor_heaps) SafeRelease(descriptor_heap);
	SafeRelease(context->graphics_command_queue);
	SafeRelease(context->device);
	
	IDXGIDebug1* debug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}
}

NativeTextureResource CreateTextureResource(GraphicsContext* api_context, TextureSize size) {
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

NativeBufferResource CreateBufferResource(GraphicsContext* api_context, u32 size, u8** cpu_address) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type                 = D3D12_HEAP_TYPE_UPLOAD;
	heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_properties.CreationNodeMask     = 0;
	heap_properties.VisibleNodeMask      = 0;
	
	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Alignment        = 0;
	resource_desc.Width            = size;
	resource_desc.Height           = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels        = 1;
	resource_desc.Format           = DXGI_FORMAT_UNKNOWN;
	resource_desc.SampleDesc       = { 1, 0 };
	resource_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.Flags            = D3D12_RESOURCE_FLAG_NONE;
	resource_desc.SamplerFeedbackMipRegion = { 0, 0, 0 };
	
	NativeBufferResource resource = {};
	auto result = context->device->CreateCommittedResource3(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&resource_desc,
		D3D12_BARRIER_LAYOUT_UNDEFINED,
		nullptr,
		nullptr,
		0,
		nullptr,
		IID_PPV_ARGS(&resource.d3d12)
	);
	DebugAssert(SUCCEEDED(result), "Failed to create buffer resource.");
	
	if (cpu_address) {
		resource.d3d12->Map(0, nullptr, (void**)cpu_address);
	}
	
	return resource;
}

void ReleaseTextureResource(GraphicsContext* context, NativeTextureResource resource) { SafeRelease(resource.d3d12); }
void ReleaseBufferResource(GraphicsContext* context, NativeBufferResource resource)   { SafeRelease(resource.d3d12); }

u32 AllocateTransientSrvDescriptorTable(GraphicsContext* api_context, u32 count) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	u32 offset = context->srv_heap_offset;
	if (offset + count > transient_srv_descriptor_count) offset = 0;
	
	context->srv_heap_offset = offset + count;
	
	return offset + persistent_srv_descriptor_count;
}

u32 AllocatePersistentSrvDescriptor(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	return ArrayPopLast(context->srv_heap_free_indices);
}

void DeallocatePersistentSrvDescriptor(GraphicsContext* api_context, u32 heap_index) {
	auto* context = (GraphicsContextD3D12*)api_context;
	ArrayAppend(context->srv_heap_free_indices, heap_index);
}

static void CreateSwapChainBackBuffers(WindowSwapChainD3D12* swap_chain, GraphicsContextD3D12* context) {
	auto* dxgi_swap_chain = swap_chain->dxgi_swap_chain;
	
	for (u32 index = 0; index < swap_chain->back_buffers.count; index += 1) {
		dxgi_swap_chain->GetBuffer(index, IID_PPV_ARGS(&swap_chain->back_buffers[index].d3d12));
	}
}

static void ReleaseSwapChainBackBuffers(WindowSwapChainD3D12* swap_chain) {
	for (auto& resource : swap_chain->back_buffers) {
		SafeRelease(resource.d3d12);
	}
}

WindowSwapChain* CreateWindowSwapChain(StackAllocator* alloc, GraphicsContext* api_context, void* hwnd, TextureFormat format) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto* swap_chain = NewFromAlloc(alloc, WindowSwapChainD3D12);
	swap_chain->size = TextureSize(format, 0, 0);
	
	IDXGIFactory4* dxgi_factory_4 = nullptr;
	defer{ SafeRelease(dxgi_factory_4); };
	
	if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&dxgi_factory_4)))) {
		DebugAssertAlways("CreateDXGIFactory failed.");
		return nullptr;
	}
	
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.Width       = 0;
	swap_chain_desc.Height      = 0;
	swap_chain_desc.Format      = dxgi_texture_format_map[(u32)ToNonSrgbFormat(format)];
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
	
	// No-op in case of R8G8B8A8_UNORM swap chain, RGB_FULL_G22_NONE_P709 is the default color space.
	auto color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	switch (swap_chain->size.format) {
	case TextureFormat::R8G8B8A8_UNORM_SRGB: color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; break;
	case TextureFormat::R16G16B16A16_FLOAT:  color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; break;
	default: DebugAssertAlways("Unhandled swap chain format. Using default G22_P709 color space."); break;
	}
	
	if (FAILED(swap_chain->dxgi_swap_chain->SetColorSpace1(color_space))) {
		DebugAssertAlways("Failed to set swap chain color space.");
	}
	
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
	
	swap_chain->size.x = size.x;
	swap_chain->size.y = size.y;
	
	CreateSwapChainBackBuffers(swap_chain, context);
}

NativeTextureResource WindowSwapGetCurrentBackBuffer(WindowSwapChain* api_swap_chain) {
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	return swap_chain->back_buffers[swap_chain->dxgi_swap_chain->GetCurrentBackBufferIndex()];
}

void WindowSwapChainBeginFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc) {
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForNextFrame(context);
	
	if (CheckShaderFileChanges(shader_compiler, alloc)) {
		WaitForLastFrame(context);
		BuildPipelineStates(context, alloc);
	}
	
	auto* command_allocator = context->command_allocators[context->frame_index % number_of_frames_in_flight];
	auto* command_list = context->command_list;
	
	command_allocator->Reset();
	command_list->Reset(command_allocator, nullptr);
	command_list->SetDescriptorHeaps(1, &context->descriptor_heaps[(u32)DescriptorHeapType::SRV]);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void WindowSwapChainEndFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc, RecordContext& record_context) {
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto& back_buffer = swap_chain->back_buffers[swap_chain->dxgi_swap_chain->GetCurrentBackBufferIndex()];
	auto* command_list = context->command_list;
	
	auto& resource_table = record_context.resource_table->virtual_resources;
	for (auto& resource : resource_table) {
		if (resource.type == VirtualResource::Type::VirtualTexture && resource.texture.size != resource.texture.allocated_size) {
			resource.texture.resource = CreateTextureResource(context, resource.texture.size);
			resource.texture.allocated_size = resource.texture.size;
		}
	}
	
	ReplayRecordContext(context, &record_context);
	
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

