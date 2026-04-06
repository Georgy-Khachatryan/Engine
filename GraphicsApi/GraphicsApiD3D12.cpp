#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMemory.h"
#include "GraphicsApiD3D12.h"
#include "RecordContext.h"
#include "ShaderCompiler.h"

#include <SDK/DLSS/include/nvsdk_ngx.h>
#include <SDK/NvAPI/include/nvapi.h>

extern "C" __declspec(dllexport) extern const UINT  D3D12SDKVersion = 618;
extern "C" __declspec(dllexport) extern const char* D3D12SDKPath    = u8".\\D3D12\\";


template<typename ResourceT>
static void SafeRelease(ResourceT*& resource) {
	if (resource) resource->Release();
	resource = nullptr;
}

static void BuildPipelineStates(GraphicsContextD3D12* context, StackAllocator* alloc, bool build_only_dirty_pipelines = true);
static void BuildRootSignatures(GraphicsContextD3D12* context, StackAllocator* alloc, ArrayView<ArrayView<u32>> root_signature_streams);
static ID3D12CommandSignature* CreateCommandSignature(ID3D12Device10* device, D3D12_INDIRECT_ARGUMENT_TYPE type, u32 byte_stride);
static CommandQueueContextD3D12 CreateCommandQueueContext(ID3D12Device10* device, D3D12_COMMAND_LIST_TYPE type);

GraphicsContext* CreateGraphicsContext(StackAllocator* alloc) {
	ProfilerScope("CreateGraphicsContext");
	
	auto* context = NewFromAlloc(alloc, GraphicsContextD3D12);
	
	ID3D12Debug* debug = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
#if BUILD_TYPE(DEBUG) || BUILD_TYPE(DEV)
		debug->EnableDebugLayer();
#elif BUILD_TYPE(PROFILE)
		// Don't enable debug layer in profile builds.
#else // !BUILD_TYPE(PROFILE)
		#error "Unknown BUILD_TYPE."
#endif // !BUILD_TYPE(PROFILE)
	}
	
	ID3D12Device10* device = nullptr;
	{
		ProfilerScope("CreateDevice");
		
		if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)))) {
			DebugAssertAlways("D3D12CreateDevice failed.");
			return nullptr;
		}
		context->device = device;
	}
	
	if (debug != nullptr) {
		ID3D12InfoQueue* info_queue = nullptr;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
			D3D12_MESSAGE_ID deny_list[] = {
				D3D12_MESSAGE_ID_OBJECT_ACCESSED_WHILE_STILL_IN_USE,
			};
			
			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.pIDList = deny_list;
			filter.DenyList.NumIDs  = ArraySize(deny_list);
			info_queue->AddStorageFilterEntries(&filter);
			
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    true);
			info_queue->Release();
		}
		debug->Release();
	}
	
	
	for (u32 i = 0; i < (u32)DescriptorHeapType::Count; i += 1) {
		ProfilerScope("CreateDescriptorHeap");
		
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
			context->srv_heap_free_indices[i] = (u16)(persistent_srv_descriptor_count - i - 1);
		}
	}
	
	
	context->graphics_context = CreateCommandQueueContext(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	context->async_copy_context = CreateCommandQueueContext(device, D3D12_COMMAND_LIST_TYPE_COPY);
	context->frame_submit_index = 1;
	context->async_submit_index = 1;
	
	
	{
		ID3D12Fence* fence = nullptr;
		if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
			DebugAssertAlways("CreateFence failed.");
			return nullptr;
		}
		context->async_copy_fence = fence;
	}
	
	
	{
		ProfilerScope("CreateCommandSignatures");
		context->dispatch_command_signature               = CreateCommandSignature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,      16);
		context->dispatch_mesh_command_signature          = CreateCommandSignature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH, 16);
		context->draw_instanced_command_signature         = CreateCommandSignature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,          16);
		context->draw_indexed_instanced_command_signature = CreateCommandSignature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,  20);
	}
	
	
	{
		extern ArrayView<ShaderDefinition> shader_definition_table;
		extern ArrayView<String>         root_signature_filenames;
		extern ArrayView<ArrayView<u32>> root_signature_streams;
		
		context->pipeline_definitions = GatherPipelineDefinitions(alloc);
		ArrayResize(context->pipeline_state_table, alloc, context->pipeline_definitions.count);
		
		context->shader_compiler = CreateShaderCompiler(alloc, root_signature_filenames, shader_definition_table, context->pipeline_definitions);
		SaveLoadShaderCache(context->shader_compiler, alloc, true);
		
		BuildRootSignatures(context, alloc, root_signature_streams);
		BuildPipelineStates(context, alloc, false);
	}
	
	{
		compile_const u32 release_queue_capacity = 512;
		ArrayReserve(context->release_queue_last_frame, alloc, release_queue_capacity);
		ArrayReserve(context->release_queue_this_frame, alloc, release_queue_capacity);
		ArrayReserve(context->release_queue_next_frame, alloc, release_queue_capacity);
	}
	
	{
		ProfilerScope("NvAPI_Initialize");
		auto result = NvAPI_Initialize();
		DebugAssert(result == NVAPI_OK, "NvAPI_Initialize failed."); // TODO: Disable NvAPI when it's not supported.
	}
	
	{
		ProfilerScope("NVSDK_NGX_D3D12_Init_with_ProjectID");
		auto result = NVSDK_NGX_D3D12_Init_with_ProjectID("99048DBD-0572-4265-B72A-B19102D347B2", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1", L"./", device);
		DebugAssert(result == NVSDK_NGX_Result_Success, "NVSDK_NGX_D3D12_Init_with_ProjectID failed."); // TODO: Disable DLSS when it's not supported.
	}
	
	return context;
}

static ID3D12CommandSignature* CreateCommandSignature(ID3D12Device10* device, D3D12_INDIRECT_ARGUMENT_TYPE type, u32 byte_stride) {
	D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
	argument_desc.Type = type;
	
	D3D12_COMMAND_SIGNATURE_DESC desc = {};
	desc.ByteStride       = byte_stride;
	desc.NumArgumentDescs = 1;
	desc.pArgumentDescs   = &argument_desc;
	desc.NodeMask         = 0;
	
	ID3D12CommandSignature* command_signature = nullptr;
	if (FAILED(device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&command_signature)))) {
		DebugAssertAlways("Failed to create command signature.");
	}
	
	return command_signature;
}

static CommandQueueContextD3D12 CreateCommandQueueContext(ID3D12Device10* device, D3D12_COMMAND_LIST_TYPE type) {
	ProfilerScope("CreateCommandQueueContext");
	
	CommandQueueContextD3D12 context;
	
	{
		D3D12_COMMAND_QUEUE_DESC queue_desc = {};
		queue_desc.Type     = type;
		queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queue_desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queue_desc.NodeMask = 0;
		
		if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&context.queue)))) {
			DebugAssertAlways("CreateCommandQueue failed.");
			return {};
		}
	}
	
	if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context.fence)))) {
		DebugAssertAlways("CreateFence failed.");
		return {};
	}
	
	for (auto& command_allocator : context.command_allocators) {
		ProfilerScope("CreateCommandAllocator");
		
		if (FAILED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&command_allocator)))) {
			DebugAssertAlways("CreateCommandAllocator failed.");
			return {};
		}
	}
	
	if (FAILED(device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&context.command_list)))) {
		DebugAssertAlways("CreateCommandList failed.");
		return {};
	}
	
	return context;
}

static void ReleaseCommandQueueContext(CommandQueueContextD3D12* context) {
	SafeRelease(context->command_list);
	for (auto& command_allocator : context->command_allocators) SafeRelease(command_allocator);
	SafeRelease(context->fence);
	SafeRelease(context->queue);
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
	ProfilerScope("CreateComputePipelineState");
	
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
	ProfilerScope("CreateGraphicsPipelineState");
	
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
	
	if (bytecode[(u32)ShaderType::VertexShader].data) {
		auto& vs = AppendSubobject<D3D12_SHADER_BYTECODE>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS);
		vs.pShaderBytecode = bytecode[(u32)ShaderType::VertexShader].data;
		vs.BytecodeLength  = bytecode[(u32)ShaderType::VertexShader].count;
	} else if (bytecode[(u32)ShaderType::MeshShader].data) {
		auto& ms = AppendSubobject<D3D12_SHADER_BYTECODE>(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS);
		ms.pShaderBytecode = bytecode[(u32)ShaderType::MeshShader].data;
		ms.BytecodeLength  = bytecode[(u32)ShaderType::MeshShader].count;
	}
	
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

static void BuildPipelineStates(GraphicsContextD3D12* context, StackAllocator* alloc, bool build_only_dirty_pipelines) {
	ProfilerScope("BuildPipelineStates");
	
	TempAllocationScope(alloc);
	auto compiled_shader_mask = CompileDirtyShaderPermutations(context->shader_compiler, alloc);
	
	for (u32 i = 0; i < context->pipeline_definitions.count; i += 1) {
		auto [bytecode, is_dirty] = GetShadersForPipelineIndex(context->shader_compiler, i, compiled_shader_mask);
		if (is_dirty == false && build_only_dirty_pipelines) continue;
		
		auto& definition = context->pipeline_definitions[i];
		if (HasAnyFlags(definition.shader_type_mask, ShaderTypeMask::ComputeShader)) {
			CreateComputePipelineState(context, bytecode, definition.root_signature_id.index, i);
		} else {
			auto pipeline_state_description = CreatePipelineStateDescription(definition.pipeline_state_stream);
			CreateGraphicsPipelineState(context, pipeline_state_description, bytecode, definition.root_signature_id.index, i);
		}
	}
}

static void BuildRootSignatures(GraphicsContextD3D12* context, StackAllocator* alloc, ArrayView<ArrayView<u32>> root_signature_streams) {
	ProfilerScope("BuildRootSignatures");
	
	FixedCapacityArray<D3D12_STATIC_SAMPLER_DESC1, 7> sampler_descs;
	auto append_sampler = [&](D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE address_mode, u32 max_anisotropy = 0) {
		auto& desc = ArrayEmplace(sampler_descs);
		desc.Filter           = filter;
		desc.AddressU         = address_mode;
		desc.AddressV         = address_mode;
		desc.AddressW         = address_mode;
		desc.MipLODBias       = 0.f;
		desc.MaxAnisotropy    = max_anisotropy;
		desc.ComparisonFunc   = D3D12_COMPARISON_FUNC_NONE;
		desc.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		desc.MinLOD           = 0.f;
		desc.MaxLOD           = D3D12_FLOAT32_MAX;
		desc.ShaderRegister   = (u32)sampler_descs.count - 1;
		desc.RegisterSpace    = 0;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.Flags            = D3D12_SAMPLER_FLAG_NONE;
	};
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	append_sampler(D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	append_sampler(D3D12_FILTER_ANISOTROPIC,        D3D12_TEXTURE_ADDRESS_MODE_WRAP, 4);
	append_sampler(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	append_sampler(D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	
	
	ArrayResize(context->root_signature_table, alloc, root_signature_streams.count);
	for (u32 root_signature_index = 0; root_signature_index < root_signature_streams.count; root_signature_index += 1) {
		TempAllocationScope(alloc);
		
		auto root_signature_stream = root_signature_streams[root_signature_index];
		
		u32 cbv_index = 0;
		u32 srv_index = 0;
		u32 uav_index = 0;
		
		Array<D3D12_ROOT_PARAMETER1> root_parameters;
		for (u32 i = 0; i < root_signature_stream.count;) {
			switch ((RootArgumentType)root_signature_stream[i++]) {
			case RootArgumentType::DescriptorTable: {
				Array<D3D12_DESCRIPTOR_RANGE1> descriptor_ranges;
				
				u32 descriptor_count = root_signature_stream[i++];
				auto last_range_type = (D3D12_DESCRIPTOR_RANGE_TYPE)u32_max;
				for (u32 descriptor_index = 0; descriptor_index < descriptor_count; descriptor_index += 1) {
					auto descriptor_type = (ResourceDescriptorType)root_signature_stream[i++];
					
					bool is_srv = HasAnyFlags(descriptor_type, ResourceDescriptorType::AnySRV);
					auto range_type = is_srv ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
					u32 base_shader_register = is_srv ? srv_index++ : uav_index++;
					
					if (last_range_type != range_type) {
						last_range_type = range_type;
						auto& descriptor_range = ArrayEmplace(descriptor_ranges, alloc);
						descriptor_range.RangeType          = range_type;
						descriptor_range.NumDescriptors     = 0;
						descriptor_range.BaseShaderRegister = base_shader_register;
						descriptor_range.RegisterSpace      = 0;
						descriptor_range.Flags              = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
						descriptor_range.OffsetInDescriptorsFromTableStart = descriptor_index;
					}
					ArrayLastElement(descriptor_ranges).NumDescriptors += 1;
				}
				
				auto& root_parameter = ArrayEmplace(root_parameters, alloc); 
				root_parameter.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				root_parameter.DescriptorTable.NumDescriptorRanges = (u32)descriptor_ranges.count;
				root_parameter.DescriptorTable.pDescriptorRanges   = descriptor_ranges.data;
				break;
			} case RootArgumentType::ConstantBuffer: {
				auto& root_parameter = ArrayEmplace(root_parameters, alloc); 
				root_parameter.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_CBV;
				root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				root_parameter.Descriptor.ShaderRegister = cbv_index++;
				root_parameter.Descriptor.RegisterSpace  = 0;
				break;
			} case RootArgumentType::PushConstantBuffer: {
				auto& root_parameter = ArrayEmplace(root_parameters, alloc); 
				root_parameter.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
				root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				root_parameter.Constants.ShaderRegister = cbv_index++;
				root_parameter.Constants.RegisterSpace  = 0;
				root_parameter.Constants.Num32BitValues = root_signature_stream[i++];
				break;
			} default: {
				DebugAssertAlways("Unexpected RootArgumentType '%'.", root_signature_stream[i]);
				i = (u32)root_signature_stream.count;
				break;
			}
			}
		}
		
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
		desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
		desc.Desc_1_2.NumParameters     = (u32)root_parameters.count;
		desc.Desc_1_2.pParameters       = root_parameters.data;
		desc.Desc_1_2.NumStaticSamplers = (u32)sampler_descs.count;
		desc.Desc_1_2.pStaticSamplers   = sampler_descs.data;
		desc.Desc_1_2.Flags             = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		
		ID3DBlob* root_signature_blob = nullptr;
		if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &root_signature_blob, nullptr))) {
			DebugAssertAlways("Failed to serialize root signature for pass.");
		}
		defer{ SafeRelease(root_signature_blob); };
		
		ID3D12RootSignature* root_signature = nullptr;
		if (FAILED(context->device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)))) {
			DebugAssertAlways("Failed to create root signature.");
		}
		
		context->root_signature_table[root_signature_index] = root_signature;
	}
}

void ReleaseGraphicsContext(GraphicsContext* api_context, StackAllocator* alloc) {
	ProfilerScope("ReleaseGraphicsContext");
	auto* context = (GraphicsContextD3D12*)api_context;
	
	SaveLoadShaderCache(context->shader_compiler, alloc, false);
	ReleaseShaderCompiler(context->shader_compiler);
	
	for (auto& root_signature : context->root_signature_table) SafeRelease(root_signature);
	for (auto& pipeline_state : context->pipeline_state_table) SafeRelease(pipeline_state);
	
	{
		ProfilerScope("NvAPI_Unload");
		auto result = NvAPI_Unload();
		DebugAssert(result == NVAPI_OK, "NvAPI_Unload failed."); // TODO: Disable NvAPI when it's not supported.
	}
	
	{
		ProfilerScope("NVSDK_NGX_D3D12_Shutdown1");
		auto result = NVSDK_NGX_D3D12_Shutdown1(context->device);
		DebugAssert(result == NVSDK_NGX_Result_Success, "NVSDK_NGX_D3D12_Shutdown1 failed."); // TODO: Disable DLSS when it's not supported.
	}
	
	SafeRelease(context->dispatch_command_signature);
	SafeRelease(context->dispatch_mesh_command_signature);
	SafeRelease(context->draw_instanced_command_signature);
	SafeRelease(context->draw_indexed_instanced_command_signature);
	
	SafeRelease(context->async_copy_fence);
	ReleaseCommandQueueContext(&context->async_copy_context);
	ReleaseCommandQueueContext(&context->graphics_context);
	for (auto& descriptor_heap : context->descriptor_heaps) SafeRelease(descriptor_heap);
	SafeRelease(context->device);
	
	IDXGIDebug1* debug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}
}

static D3D12_RESOURCE_FLAGS TranslateCreateResourceFlags(CreateResourceFlags flags) {
	auto result = D3D12_RESOURCE_FLAG_NONE;
	
	if (HasAnyFlags(flags, CreateResourceFlags::DSV)) result |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if (HasAnyFlags(flags, CreateResourceFlags::RTV)) result |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (HasAnyFlags(flags, CreateResourceFlags::UAV)) result |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	
	return result;
}

NativeTextureResource CreateTextureResource(GraphicsContext* api_context, TextureSize size, CreateResourceFlags flags) {
	ProfilerScope("CreateTextureResource");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	
	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension        = size.type == TextureSize::Type::Texture3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource_desc.Alignment        = 0;
	resource_desc.Width            = size.x;
	resource_desc.Height           = size.y;
	resource_desc.DepthOrArraySize = size.z;
	resource_desc.MipLevels        = size.mips;
	resource_desc.Format           = dxgi_texture_format_map[(u32)size.format];
	resource_desc.SampleDesc       = { 1, 0 };
	resource_desc.Layout           = HasAnyFlags(flags, CreateResourceFlags::Sparse) ? D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE : D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.Flags            = TranslateCreateResourceFlags(flags);
	resource_desc.SamplerFeedbackMipRegion = { 0, 0, 0 };
	
	D3D12_CLEAR_VALUE clear_value = {};
	clear_value.Format = resource_desc.Format;
	bool set_clear_value = HasAnyFlags(flags, CreateResourceFlags::RTV | CreateResourceFlags::DSV);
	
	NativeTextureResource resource = {};
	if (HasAnyFlags(flags, CreateResourceFlags::Sparse) == false) {
		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
		heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_properties.CreationNodeMask     = 0;
		heap_properties.VisibleNodeMask      = 0;
		
		auto result = context->device->CreateCommittedResource3(
			&heap_properties,
			D3D12_HEAP_FLAG_NONE,
			&resource_desc,
			D3D12_BARRIER_LAYOUT_COMMON,
			set_clear_value ? &clear_value : nullptr,
			nullptr,
			0,
			nullptr,
			IID_PPV_ARGS(&resource.d3d12)
		);
		DebugAssert(SUCCEEDED(result), "Failed to create texture resource.");
	} else {
		auto result = context->device->CreateReservedResource2(
			(D3D12_RESOURCE_DESC*)&resource_desc,
			D3D12_BARRIER_LAYOUT_COMMON,
			set_clear_value ? &clear_value : nullptr,
			nullptr,
			0,
			nullptr,
			IID_PPV_ARGS(&resource.d3d12)
		);
		DebugAssert(SUCCEEDED(result), "Failed to create texture resource.");
	}
	
	return resource;
}

NativeBufferResource CreateBufferResource(GraphicsContext* api_context, u32 size, CreateResourceFlags flags, u8** cpu_address) {
	ProfilerScope("CreateBufferResource");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	
	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
	heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_properties.CreationNodeMask     = 0;
	heap_properties.VisibleNodeMask      = 0;
	
	if (HasAllFlags(flags, CreateResourceFlags::Readback | CreateResourceFlags::UAV)) {
		heap_properties.Type                 = D3D12_HEAP_TYPE_CUSTOM;
		heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	} else if (HasAnyFlags(flags, CreateResourceFlags::Readback)) {
		heap_properties.Type                 = D3D12_HEAP_TYPE_READBACK;
	} else if (HasAnyFlags(flags, CreateResourceFlags::Upload)) {
		heap_properties.Type                 = D3D12_HEAP_TYPE_UPLOAD;
	}
	
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
	resource_desc.Flags            = TranslateCreateResourceFlags(flags);
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
	
	if (cpu_address && HasAnyFlags(flags, CreateResourceFlags::Upload | CreateResourceFlags::Readback)) {
		resource.d3d12->Map(0, nullptr, (void**)cpu_address);
	}
	
	return resource;
}

NativeMemoryResource CreateMemoryResource(GraphicsContext* api_context, u64 size) {
	ProfilerScope("CreateMemoryResource");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	
	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
	heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_properties.CreationNodeMask     = 0;
	heap_properties.VisibleNodeMask      = 0;
	
	D3D12_HEAP_DESC heap_desc = {};
	heap_desc.SizeInBytes                     = size;
	heap_desc.Properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
	heap_desc.Properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_desc.Properties.CreationNodeMask     = 0;
	heap_desc.Properties.VisibleNodeMask      = 0;
	heap_desc.Alignment                       = 0;
	heap_desc.Flags                           = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
	
	NativeMemoryResource resource = {};
	auto result = context->device->CreateHeap(&heap_desc, IID_PPV_ARGS(&resource.d3d12));
	DebugAssert(SUCCEEDED(result), "Failed to create memory resource.");
	
	return resource;
}

SparseTextureLayout GetSparseTextureLayout(GraphicsContext* api_context, NativeTextureResource resource) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	u32 tile_count = 0;
	D3D12_PACKED_MIP_INFO packed_mip_info = {};
	D3D12_TILE_SHAPE tile_shape = {};
	context->device->GetResourceTiling(resource.d3d12, &tile_count, &packed_mip_info, &tile_shape, nullptr, 0, nullptr);
	
	SparseTextureLayout result;
	result.tile_shape        = u16x2(tile_shape.WidthInTexels, tile_shape.HeightInTexels);
	result.packed_tile_count = (u16)packed_mip_info.NumTilesForPackedMips;
	result.packed_mip_count  = packed_mip_info.NumPackedMips;
	result.regular_mip_count = packed_mip_info.NumStandardMips;
	
	return result;
}

static void ReleaseResource(GraphicsContextD3D12* context, ID3D12Pageable* resource, ResourceReleaseCondition condition) {
	if (resource == nullptr) return;
	
	ProfilerScope("ReleaseResource");
	
	switch (condition) {
	case ResourceReleaseCondition::None: resource->Release(); break;
	case ResourceReleaseCondition::EndOfLastGpuFrame: ArrayAppend(context->release_queue_last_frame, resource); break;
	case ResourceReleaseCondition::EndOfThisGpuFrame: ArrayAppend(context->release_queue_this_frame, resource); break;
	case ResourceReleaseCondition::EndOfNextGpuFrame: ArrayAppend(context->release_queue_next_frame, resource); break;
	}
}

static void CycleResourceReleaseQueues(GraphicsContextD3D12* context) {
	ProfilerScope("CycleResourceReleaseQueues");
	
	static_assert(number_of_frames_in_flight == 2);
	for (auto* resource : context->release_queue_last_frame) {
		resource->Release();
	}
	context->release_queue_last_frame.count = 0;
	
	Swap(context->release_queue_last_frame, context->release_queue_next_frame);
	Swap(context->release_queue_this_frame, context->release_queue_last_frame);
}

void ReleaseTextureResource(GraphicsContext* context, NativeTextureResource resource, ResourceReleaseCondition condition) {
	ReleaseResource((GraphicsContextD3D12*)context, resource.d3d12, condition);
}

void ReleaseBufferResource(GraphicsContext* context, NativeBufferResource resource, ResourceReleaseCondition condition) {
	ReleaseResource((GraphicsContextD3D12*)context, resource.d3d12, condition);
}

void ReleaseMemoryResource(GraphicsContext* context, NativeMemoryResource resource, ResourceReleaseCondition condition) {
	ReleaseResource((GraphicsContextD3D12*)context, resource.d3d12, condition);
}

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


static void WaitForLastSubmit(CommandQueueContextD3D12* context, u64 submit_index) {
	if (submit_index <= 1) return;
	context->fence->SetEventOnCompletion(submit_index - 1, nullptr);
}

static void WaitForNextSubmit(CommandQueueContextD3D12* context, u64 submit_index) {
	if (submit_index <= number_of_frames_in_flight) return;
	context->fence->SetEventOnCompletion(submit_index - number_of_frames_in_flight, nullptr);
}

static void WaitForLastFrame(GraphicsContextD3D12* context) {
	ProfilerScope("WaitForLastFrame");
	WaitForLastSubmit(&context->graphics_context, context->frame_submit_index);
}

static void WaitForNextFrame(GraphicsContextD3D12* context) {
	ProfilerScope("WaitForNextFrame");
	WaitForNextSubmit(&context->graphics_context, context->frame_submit_index);
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

static void SetSwapChainColorSpaceForFormat(WindowSwapChainD3D12* swap_chain, TextureFormat format) {
	if (swap_chain->size.format == format) return;
	
	auto color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	switch (format) {
	case TextureFormat::R8G8B8A8_UNORM_SRGB: color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; break;
	case TextureFormat::R16G16B16A16_FLOAT:  color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; break;
	default: DebugAssertAlways("Unhandled swap chain format. Using default G22_P709 color space."); break;
	}
	
	if (FAILED(swap_chain->dxgi_swap_chain->SetColorSpace1(color_space))) {
		DebugAssertAlways("Failed to set swap chain color space.");
	}
}

WindowSwapChain* CreateWindowSwapChain(StackAllocator* alloc, GraphicsContext* api_context, void* hwnd, TextureFormat format) {
	ProfilerScope("CreateWindowSwapChain");
	
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
	if (FAILED(dxgi_factory_4->CreateSwapChainForHwnd(context->graphics_context.queue, (HWND)hwnd, &swap_chain_desc, nullptr, nullptr, &dxgi_swap_chain_1))) {
		DebugAssertAlways("CreateSwapChainForHwnd failed.");
		return nullptr;
	}
	
	if (FAILED(dxgi_swap_chain_1->QueryInterface(IID_PPV_ARGS(&swap_chain->dxgi_swap_chain)))) {
		DebugAssertAlways("SwapChain QueryInterface failed.");
		return nullptr;
	}
	dxgi_swap_chain_1->Release();
	
	SetSwapChainColorSpaceForFormat(swap_chain, format);
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

void ResizeWindowSwapChain(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, uint2 size, TextureFormat format) {
	if (api_swap_chain->size.x == size.x && api_swap_chain->size.y == size.y && api_swap_chain->size.format == format) return;
	
	ProfilerScope("ResizeWindowSwapChain");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForLastFrame(context);
	
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	ReleaseSwapChainBackBuffers(swap_chain);
	
	if (FAILED(swap_chain->dxgi_swap_chain->ResizeBuffers(0, size.x, size.y, dxgi_texture_format_map[(u32)ToNonSrgbFormat(format)], 0))) {
		DebugAssertAlways("ResizeBuffers failed.");
		return;
	}
	
	SetSwapChainColorSpaceForFormat(swap_chain, format);
	
	swap_chain->size = TextureSize(format, size.x, size.y);
	
	CreateSwapChainBackBuffers(swap_chain, context);
}

NativeTextureResource WindowSwapGetCurrentBackBuffer(WindowSwapChain* api_swap_chain) {
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	return swap_chain->back_buffers[swap_chain->dxgi_swap_chain->GetCurrentBackBufferIndex()];
}

static void ResetCommandQueueContext(GraphicsContextD3D12* context, CommandQueueContextD3D12* queue_context, u64 submit_index, D3D12_COMMAND_LIST_TYPE type) {
	ProfilerScope("ResetCommandQueueContext");
	
	auto* command_allocator = queue_context->command_allocators[submit_index % number_of_frames_in_flight];
	auto* command_list      = queue_context->command_list;
	
	command_allocator->Reset();
	command_list->Reset(command_allocator, nullptr);
	
	if (type != D3D12_COMMAND_LIST_TYPE_COPY) {
		command_list->SetDescriptorHeaps(1, &context->descriptor_heaps[(u32)DescriptorHeapType::SRV]);
		command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}

static void SubmitCommandQueueContext(CommandQueueContextD3D12* context) {
	auto* command_list  = context->command_list;
	auto* command_queue = context->queue;
	
	command_list->Close();
	command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_list);
}

void WindowSwapChainBeginFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc) {
	ProfilerScope("WindowSwapChainBeginFrame");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	WaitForNextFrame(context);
	
	CycleResourceReleaseQueues(context);
	
	if (CheckShaderFileChanges(context->shader_compiler, alloc)) {
		WaitForLastFrame(context);
		BuildPipelineStates(context, alloc);
	}
	
	ResetCommandQueueContext(context, &context->graphics_context, context->frame_submit_index, D3D12_COMMAND_LIST_TYPE_DIRECT);
}

void WindowSwapChainEndFrame(WindowSwapChain* api_swap_chain, GraphicsContext* api_context, StackAllocator* alloc, RecordContext* record_context) {
	ProfilerScope("WindowSwapChainEndFrame");
	
	auto* swap_chain = (WindowSwapChainD3D12*)api_swap_chain;
	auto* context    = (GraphicsContextD3D12*)api_context;
	
	ReplayRecordContext(context, record_context);
	SubmitCommandQueueContext(&context->graphics_context);
	
	u32 sync_interval = 1;
	if (FAILED(swap_chain->dxgi_swap_chain->Present(sync_interval, 0))) {
		DebugAssertAlways("Present failed.");
	}
	
	auto* command_queue = context->graphics_context.queue;
	command_queue->Signal(context->graphics_context.fence, context->frame_submit_index);
	
	context->frame_submit_index += 1;
}

void WaitForInFlightSubmits(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	WaitForLastFrame(context);
	WaitForLastSubmit(&context->async_copy_context, context->async_submit_index);
}

void SubmitAsyncCopyCommands(GraphicsContext* api_context, ArrayView<AsyncCopyBufferToBufferCommand> copy_buffer_to_buffer_commands, ArrayView<AsyncCopyBufferToTextureCommand> copy_buffer_to_texture_commands, u64 async_copy_signal_index) {
	ProfilerScope("SubmitAsyncCopyCommands");
	
	auto* context      = (GraphicsContextD3D12*)api_context;
	auto* command_list = context->async_copy_context.command_list;
	
	WaitForNextSubmit(&context->async_copy_context, context->async_submit_index);
	ResetCommandQueueContext(context, &context->async_copy_context, context->async_submit_index, D3D12_COMMAND_LIST_TYPE_COPY);
	
	ProfilerBeginScope("SubmitAsyncCopyCommands", command_list);
	for (auto& command : copy_buffer_to_buffer_commands) {
		command_list->CopyBufferRegion(command.dst_resource.d3d12, command.dst_offset, command.src_resource.d3d12, command.src_offset, command.size);
	}
	
	for (auto& command : copy_buffer_to_texture_commands) {
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource                          = command.src_resource.d3d12;
		src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset             = command.src_offset;
		src.PlacedFootprint.Footprint.Format   = dxgi_texture_format_map[(u32)command.format];
		src.PlacedFootprint.Footprint.Width    = command.src_size.x;
		src.PlacedFootprint.Footprint.Height   = command.src_size.y;
		src.PlacedFootprint.Footprint.Depth    = command.src_size.z;
		src.PlacedFootprint.Footprint.RowPitch = command.src_row_pitch;
		
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource        = command.dst_resource.d3d12;
		dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = command.dst_subresource_index;
		
		command_list->CopyTextureRegion(&dst, command.dst_offset.x, command.dst_offset.y, command.dst_offset.z, &src, nullptr);
	}
	ProfilerEndScope(command_list);
	
	SubmitCommandQueueContext(&context->async_copy_context);
	
	auto* command_queue = context->async_copy_context.queue;
	command_queue->Signal(context->async_copy_context.fence, context->async_submit_index);
	command_queue->Signal(context->async_copy_fence,         async_copy_signal_index);
	
	context->async_submit_index += 1;
}

void AsyncUpdateMemoryMappings(GraphicsContext* api_context, StackAllocator* alloc, ArrayView<u32> tile_indices, u32 subresource_index, NativeTextureResource resource, NativeMemoryResource memory) {
	ProfilerScope("AsyncUpdateMemoryMappings");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	auto* command_queue = context->async_copy_context.queue;
	
	D3D12_TILED_RESOURCE_COORDINATE subresource_coordinates = {};
	subresource_coordinates.Subresource = subresource_index;
	
	D3D12_TILE_REGION_SIZE tile_region_size = {};
	tile_region_size.NumTiles = (u32)tile_indices.count;
	
	TempAllocationScope(alloc);
	
	auto tile_counts = ArrayViewAllocate<u32>(alloc, tile_indices.count);
	auto tile_flags  = ArrayViewAllocate<D3D12_TILE_RANGE_FLAGS>(alloc, tile_indices.count);
	
	for (u32 i = 0; i < tile_indices.count; i += 1) {
		tile_counts[i] = 1;
		tile_flags[i]  = D3D12_TILE_RANGE_FLAG_NONE;
	}
	
	command_queue->UpdateTileMappings(
		resource.d3d12,
		1,
		&subresource_coordinates,
		&tile_region_size,
		memory.d3d12,
		(u32)tile_indices.count,
		tile_flags.data,
		tile_indices.data,
		tile_counts.data,
		D3D12_TILE_MAPPING_FLAG_NONE
	);
}

u64 GetCompletedAsyncCopyCommandValue(GraphicsContext* api_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	return context->async_copy_fence->GetCompletedValue();
}
