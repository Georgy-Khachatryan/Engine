#include "GraphicsApiD3D12.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicMath.h"
#include "RecordContext.h"
#include "RecordContextCommands.h"

#include <SDK/DLSS/include/nvsdk_ngx_helpers.h>
#include <SDK/XeSS/include/xess_d3d12.h>
#include <SDK/NvApi/include/nvapi.h>

static u64 ComputeGpuVirtualAddress(GpuAddress gpu_address, ArrayView<VirtualResource> resources) {
	auto& resource = resources[(u32)gpu_address.resource_id];
	return resource.buffer.resource.d3d12->GetGPUVirtualAddress() + gpu_address.offset;
}

static void CreateDescriptorTables(GraphicsContextD3D12* context, ArrayView<HLSL::BaseDescriptorTable*> descriptor_tables, ArrayView<VirtualResource> resources) {
	ProfilerScope("CreateDescriptorTables");
	
	auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::SRV];
	auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
	auto* device = context->device;
	
	for (auto* descriptor_table : descriptor_tables) {
		auto descriptors = ArrayView<ResourceDescriptor>{ (ResourceDescriptor*)(descriptor_table + 1), (u64)descriptor_table->descriptor_count };
		
		auto descriptor_table_handle = cpu_base_handle;
		descriptor_table_handle.ptr += descriptor_table->descriptor_heap_offset * descriptor_size;
		
		for (auto& descriptor : descriptors) {
			auto& resource = resources[(u32)descriptor.resource_id];
			
			switch (descriptor.common.type) {
			case ResourceDescriptorType::Texture2D: {
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				desc.Format                        = resource.texture.resource.d3d12 ? dxgi_texture_format_map[(u32)ToSrvFormat(resource.texture.size.format)] : DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.Texture2D.MostDetailedMip     = descriptor.texture.mip_index;
				desc.Texture2D.MipLevels           = Math::Min(descriptor.texture.mip_count, (u8)(resource.texture.size.mips - descriptor.texture.mip_index));
				desc.Texture2D.PlaneSlice          = 0;
				desc.Texture2D.ResourceMinLODClamp = 0.f;
				
				device->CreateShaderResourceView(resource.texture.resource.d3d12, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::RWTexture2D: {
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				desc.Format               = resource.texture.resource.d3d12 ? dxgi_texture_format_map[(u32)resource.texture.size.format] : DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipSlice   = descriptor.texture.mip_index;
				desc.Texture2D.PlaneSlice = 0;
				
				device->CreateUnorderedAccessView(resource.texture.resource.d3d12, nullptr, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::RegularBuffer: {
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				desc.Format                     = DXGI_FORMAT_UNKNOWN;
				desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
				desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.Buffer.FirstElement        = descriptor.buffer.offset / descriptor.buffer.stride;
				desc.Buffer.NumElements         = Math::Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / descriptor.buffer.stride;
				desc.Buffer.StructureByteStride = descriptor.buffer.stride;
				desc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
				DebugAssert(descriptor.buffer.offset % descriptor.buffer.stride == 0, "RegularBuffer offset is not correctly aligned.");
				
				device->CreateShaderResourceView(resource.buffer.resource.d3d12, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::RWRegularBuffer: {
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				desc.Format                      = DXGI_FORMAT_UNKNOWN;
				desc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
				desc.Buffer.FirstElement         = descriptor.buffer.offset / descriptor.buffer.stride;
				desc.Buffer.NumElements          = Math::Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / descriptor.buffer.stride;
				desc.Buffer.StructureByteStride  = descriptor.buffer.stride;
				desc.Buffer.CounterOffsetInBytes = 0;
				desc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
				DebugAssert(descriptor.buffer.offset % descriptor.buffer.stride == 0, "RWRegularBuffer offset is not correctly aligned.");
				
				device->CreateUnorderedAccessView(resource.buffer.resource.d3d12, nullptr, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::ByteBuffer: {
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				desc.Format                     = DXGI_FORMAT_R32_TYPELESS;
				desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
				desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.Buffer.FirstElement        = descriptor.buffer.offset / sizeof(u32);
				desc.Buffer.NumElements         = Math::Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / sizeof(u32);
				desc.Buffer.StructureByteStride = 0;
				desc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
				DebugAssert(descriptor.buffer.offset % 16 == 0, "ByteBuffer offset is not correctly aligned.");
				
				device->CreateShaderResourceView(resource.buffer.resource.d3d12, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::RWByteBuffer: {
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				desc.Format                      = DXGI_FORMAT_R32_TYPELESS;
				desc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
				desc.Buffer.FirstElement         = descriptor.buffer.offset / sizeof(u32);
				desc.Buffer.NumElements          = Math::Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / sizeof(u32);
				desc.Buffer.StructureByteStride  = 0;
				desc.Buffer.CounterOffsetInBytes = 0;
				desc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;
				DebugAssert(descriptor.buffer.offset % 16 == 0, "RWByteBuffer offset is not correctly aligned.");
				
				device->CreateUnorderedAccessView(resource.buffer.resource.d3d12, nullptr, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::TopLevelRTAS: {
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				desc.Format                  = DXGI_FORMAT_UNKNOWN;
				desc.ViewDimension           = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.RaytracingAccelerationStructure.Location = ComputeGpuVirtualAddress(GpuAddress(descriptor.resource_id, descriptor.buffer.offset), resources);
				DebugAssert(desc.RaytracingAccelerationStructure.Location % D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT == 0, "TopLevelRTAS offset is not correctly aligned.");
				
				device->CreateShaderResourceView(nullptr, &desc, descriptor_table_handle);
				break;
			} default: {
				DebugAssertAlways("Unhandled ResourceDescriptorType '%'.", (u32)descriptor.common.type);
				break;
			}
			}
			
			descriptor_table_handle.ptr += descriptor_size;
		}
	}
	
}

static void CreateRenderTargetView(GraphicsContextD3D12* context, VirtualResource& resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle) {
	DebugAssert(resource.texture.size.type == TextureSize::Type::Texture2D, "Only 2D texture render targets are implemented.");
	
	D3D12_RENDER_TARGET_VIEW_DESC desc = {};
	desc.Format               = dxgi_texture_format_map[(u32)resource.texture.size.format];
	desc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipSlice   = 0;
	desc.Texture2D.PlaneSlice = 0;
	context->device->CreateRenderTargetView(resource.texture.resource.d3d12, &desc, descriptor_handle);
}

static void CreateDepthStencilView(GraphicsContextD3D12* context, VirtualResource& resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle) {
	DebugAssert(resource.texture.size.type == TextureSize::Type::Texture2D, "Only 2D texture render targets are implemented.");
	
	D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
	desc.Format             = dxgi_texture_format_map[(u32)resource.texture.size.format];
	desc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipSlice = 0;
	context->device->CreateDepthStencilView(resource.texture.resource.d3d12, &desc, descriptor_handle);
}


static void CmdClearRenderTargetD3D12(CmdClearRenderTargetPacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context, ArrayView<VirtualResource> resources) {
	auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::RTV];
	CreateRenderTargetView(context, resources[(u32)packet->resource_id], cpu_base_handle);
	
	auto clear_color = float4(0.f);
	command_list->ClearRenderTargetView(cpu_base_handle, &clear_color.x, 0, nullptr);
}

static void CmdClearDepthStencilD3D12(CmdClearDepthStencilPacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context, ArrayView<VirtualResource> resources) {
	auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::DSV];
	CreateDepthStencilView(context, resources[(u32)packet->resource_id], cpu_base_handle);
	
	command_list->ClearDepthStencilView(cpu_base_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.f, 0, 0, nullptr);
}

static void CmdSetRenderTargetsD3D12(CmdSetRenderTargetsPacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context, ArrayView<VirtualResource> resources) {
	auto rtv_cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::RTV];
	auto rtv_descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::RTV];
	
	auto rtv_descriptor_handle = rtv_cpu_base_handle;
	for (auto resource_id : packet->resource_ids) {
		CreateRenderTargetView(context, resources[(u32)resource_id], rtv_descriptor_handle);
		rtv_descriptor_handle.ptr += rtv_descriptor_size;
	}
	
	auto dsv_cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::DSV];
	bool set_depth_stencil = packet->depth_stencil_resource_id != (VirtualResourceID)0;
	if (set_depth_stencil) CreateDepthStencilView(context, resources[(u32)packet->depth_stencil_resource_id], dsv_cpu_base_handle);
	
	command_list->OMSetRenderTargets((u32)packet->resource_ids.count, &rtv_cpu_base_handle, true, set_depth_stencil ? &dsv_cpu_base_handle : nullptr);
}

static void CmdSetViewportD3D12(CmdSetViewportAndScissorPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = (float)packet->min.x;
	viewport.TopLeftY = (float)packet->min.y;
	viewport.Width    = (float)(packet->max.x - packet->min.x);
	viewport.Height   = (float)(packet->max.y - packet->min.y);
	viewport.MinDepth = 0.f;
	viewport.MaxDepth = 1.f;
	command_list->RSSetViewports(1, &viewport);
}

static void CmdSetScissorD3D12(CmdSetViewportAndScissorPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	D3D12_RECT scissor = {};
	scissor.left   = (s32)packet->min.x;
	scissor.top    = (s32)packet->min.y;
	scissor.right  = (s32)packet->max.x;
	scissor.bottom = (s32)packet->max.y;
	command_list->RSSetScissorRects(1, &scissor);
}

static void CmdSetIndexBufferViewD3D12(CmdSetIndexBufferViewPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	D3D12_INDEX_BUFFER_VIEW index_buffer_view = {};
	index_buffer_view.BufferLocation = ComputeGpuVirtualAddress(packet->gpu_address, resources);
	index_buffer_view.SizeInBytes    = packet->size;
	index_buffer_view.Format         = dxgi_texture_format_map[(u32)packet->format];
	command_list->IASetIndexBuffer(&index_buffer_view);
}

static void CmdDispatchD3D12(CmdDispatchPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->Dispatch(packet->group_count.x, packet->group_count.y, packet->group_count.z);
}

static void CmdDispatchMeshD3D12(CmdDispatchMeshPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DispatchMesh(packet->group_count.x, packet->group_count.y, packet->group_count.z);
}

static void CmdDrawInstancedD3D12(CmdDrawInstancedPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DrawInstanced(packet->vertex_count_per_instance, packet->instance_count, packet->start_vertex_location, packet->start_instance_location);
}

static void CmdDrawIndexedInstancedD3D12(CmdDrawIndexedInstancedPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DrawIndexedInstanced(packet->index_count_per_instance, packet->instance_count, packet->start_index_location, packet->base_vertex_location, packet->start_instance_location);
}

static void CmdExecuteIndirectD3D12(CmdExecuteIndirectPacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context, ArrayView<VirtualResource> resources) {
	ID3D12CommandSignature* command_signature = nullptr;
	
	switch (packet->indirect_command_type) {
	case CommandType::Dispatch: command_signature = context->dispatch_command_signature; break;
	case CommandType::DispatchMesh: command_signature = context->dispatch_mesh_command_signature; break;
	case CommandType::DrawInstanced: command_signature = context->draw_instanced_command_signature; break;
	case CommandType::DrawIndexedInstanced: command_signature = context->draw_indexed_instanced_command_signature; break;
	default: DebugAssertAlways("Unsupported indirect command type '%'.", (u32)packet->indirect_command_type);
	}
	
	auto& indirect_arguments = resources[(u32)packet->indirect_arguments.resource_id];
	command_list->ExecuteIndirect(command_signature, 1, indirect_arguments.buffer.resource.d3d12, packet->indirect_arguments.offset, nullptr, 0);
}

static NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS TranslateBuildLimitsMeshletRTAS(const BuildLimitsMeshletRTAS& limits) {
	NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = {};
	inputs.maxArgCount                                = limits.max_meshlet_count;
	inputs.flags                                      = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
	inputs.type                                       = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES;
	inputs.mode                                       = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
	inputs.trianglesDesc.vertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
	inputs.trianglesDesc.maxGeometryIndexValue        = 0;
	inputs.trianglesDesc.maxUniqueGeometryCountPerArg = 0;
	inputs.trianglesDesc.maxTriangleCountPerArg       = Math::Min(max_triangles_per_meshlet, limits.max_total_triangle_count);
	inputs.trianglesDesc.maxVertexCountPerArg         = Math::Min(max_vertices_per_meshlet,  limits.max_total_vertex_count);
	inputs.trianglesDesc.maxTotalTriangleCount        = limits.max_total_triangle_count;
	inputs.trianglesDesc.maxTotalVertexCount          = limits.max_total_vertex_count;
	inputs.trianglesDesc.minPositionTruncateBitCount  = 0;
	
	return inputs;
}

static NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS TranslateMoveLimitsMeshletRTAS(const MoveLimitsMeshletRTAS& limits) {
	NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = {};
	inputs.maxArgCount             = limits.max_meshlet_count;
	inputs.flags                   = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE;
	inputs.type                    = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT;
	inputs.mode                    = limits.result_type == IndirectRtasResultType::Explicit ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
	inputs.movesDesc.type          = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_CLUSTER_LEVEL_ACCELERATION_STRUCTURE;
	inputs.movesDesc.maxBytesMoved = limits.rtas_max_size_bytes;
	
	return inputs;
}

static NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS TranslateBuildLimitsMeshletBLAS(const BuildLimitsMeshletBLAS& limits) {
	NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = {};
	inputs.maxArgCount                 = limits.max_blas_count;
	inputs.flags                       = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
	inputs.type                        = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;
	inputs.mode                        = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
	inputs.clasDesc.maxTotalClasCount  = limits.max_total_meshlet_count;
	inputs.clasDesc.maxClasCountPerArg = limits.max_meshlets_per_blas;
	
	return inputs;
}

static NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX TranslateBuildLimitsTLAS(const BuildLimitsTLAS& limits) {
	NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX inputs = {};
	inputs.type                      = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.flags                     = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE_EX;
	inputs.numDescs                  = limits.blas_instance_count;
	inputs.descsLayout               = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.geometryDescStrideInBytes = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
	inputs.instanceDescs             = 0;
	
	return inputs;
}

static MemoryRequirementsRTAS GetRtasMemoryRequirements(GraphicsContext* api_context, const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO requirements = {};
	
	NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS requirements_params = {};
	requirements_params.version = NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER;
	requirements_params.pInput  = &inputs;
	requirements_params.pInfo   = &requirements;
	
	auto result = NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(context->device, &requirements_params);
	DebugAssert(result == NVAPI_OK, "NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo failed.");
	
	MemoryRequirementsRTAS memory_requirements;
	memory_requirements.rtas_max_size_bytes = (u32)requirements.resultDataMaxSizeInBytes;
	memory_requirements.scratch_size_bytes  = (u32)requirements.scratchDataSizeInBytes;
	
	return memory_requirements;
}

MemoryRequirementsRTAS GetMeshletRtasMemoryRequirements(GraphicsContext* api_context, const BuildLimitsMeshletRTAS& limits) {
	auto inputs = TranslateBuildLimitsMeshletRTAS(limits);
	return GetRtasMemoryRequirements(api_context, inputs);
}

MemoryRequirementsRTAS GetMeshletRtasMemoryRequirements(GraphicsContext* api_context, const MoveLimitsMeshletRTAS& limits) {
	auto inputs = TranslateMoveLimitsMeshletRTAS(limits);
	return GetRtasMemoryRequirements(api_context, inputs);
}

MemoryRequirementsRTAS GetMeshletBlasMemoryRequirements(GraphicsContext* api_context, const BuildLimitsMeshletBLAS& limits) {
	auto inputs = TranslateBuildLimitsMeshletBLAS(limits);
	return GetRtasMemoryRequirements(api_context, inputs);
}

MemoryRequirementsRTAS GetTlasMemoryRequirements(GraphicsContext* api_context, const BuildLimitsTLAS& limits) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto inputs = TranslateBuildLimitsTLAS(limits);
	
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO requirements = {};
	
	NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS params = {};
	params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
	params.pDesc   = &inputs;
	params.pInfo   = &requirements;
	
	auto result = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(context->device, &params);
	DebugAssert(result == NVAPI_OK, "NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo failed.");
	
	MemoryRequirementsRTAS memory_requirements;
	memory_requirements.rtas_max_size_bytes = (u32)requirements.ResultDataMaxSizeInBytes;
	memory_requirements.scratch_size_bytes  = (u32)requirements.ScratchDataSizeInBytes;
	
	return memory_requirements;
}

static void CmdBuildMeshletRtasD3D12(CmdBuildMeshletRtasPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	ProfilerScope("CmdBuildMeshletRtasD3D12", command_list);
	
	for (auto& inputs : packet->inputs) {
		u64 dst_meshlet_descs  = ComputeGpuVirtualAddress(inputs.dst_meshlet_descs,  resources);
		u64 indirect_arguments = ComputeGpuVirtualAddress(inputs.indirect_arguments, resources);
		
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC desc = {};
		desc.inputs                  = TranslateBuildLimitsMeshletRTAS(inputs.limits);
		desc.addressResolutionFlags  = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE;
		desc.batchResultData         = ComputeGpuVirtualAddress(inputs.meshlet_rtas, resources);
		desc.batchScratchData        = ComputeGpuVirtualAddress(inputs.scratch_data, resources);
		desc.destinationAddressArray = { dst_meshlet_descs + 0, 16 };
		desc.resultSizeArray         = { dst_meshlet_descs + 8, 16 };
		desc.indirectArgArray        = { indirect_arguments, sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS) };
		desc.indirectArgCount        = 0;
		
		NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS params = {};
		params.version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER;
		params.pDesc   = &desc;
		
		auto result = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(command_list, &params);
		DebugAssert(result == NVAPI_OK, "NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation failed.");
	}
}

static void CmdMoveMeshletRtasD3D12(CmdMoveMeshletRtasPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	ProfilerScope("CmdMoveMeshletRtasD3D12", command_list);
	
	for (auto& inputs : packet->inputs) {
		u64 src_meshlet_descs = ComputeGpuVirtualAddress(inputs.src_meshlet_descs, resources);
		u64 dst_meshlet_descs = ComputeGpuVirtualAddress(inputs.dst_meshlet_descs, resources);
		
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC desc = {};
		desc.inputs                  = TranslateMoveLimitsMeshletRTAS(inputs.limits);
		desc.addressResolutionFlags  = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE;
		desc.batchResultData         = inputs.limits.result_type == IndirectRtasResultType::Explicit ? 0 : ComputeGpuVirtualAddress(inputs.meshlet_rtas, resources);
		desc.batchScratchData        = ComputeGpuVirtualAddress(inputs.scratch_data, resources);
		desc.destinationAddressArray = { dst_meshlet_descs + 0, inputs.limits.result_type == IndirectRtasResultType::Explicit ? 8u : 16u };
		desc.resultSizeArray         = { (inputs.limits.result_type == IndirectRtasResultType::Explicit ? 0u : dst_meshlet_descs + 8u), (inputs.limits.result_type == IndirectRtasResultType::Explicit ? 0u : 16u) };
		desc.indirectArgArray        = { src_meshlet_descs + 0, inputs.limits.result_type == IndirectRtasResultType::Explicit ? 8u : 16u };
		desc.indirectArgCount        = 0;
		
		NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS params = {};
		params.version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER;
		params.pDesc   = &desc;
		
		auto result = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(command_list, &params);
		DebugAssert(result == NVAPI_OK, "NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation failed.");
	}
}

static void CmdBuildMeshletBlasD3D12(CmdBuildMeshletBlasPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	ProfilerScope("CmdBuildMeshletBlasD3D12", command_list);
	auto& inputs = packet->inputs;
	
	u64 dst_blas_descs     = ComputeGpuVirtualAddress(inputs.dst_blas_descs,     resources);
	u64 indirect_arguments = ComputeGpuVirtualAddress(inputs.indirect_arguments, resources);
	
	NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC desc = {};
	desc.inputs                  = TranslateBuildLimitsMeshletBLAS(inputs.limits);
	desc.addressResolutionFlags  = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE;
	desc.batchResultData         = ComputeGpuVirtualAddress(inputs.meshlet_blas, resources);
	desc.batchScratchData        = ComputeGpuVirtualAddress(inputs.scratch_data, resources);
	desc.destinationAddressArray = { dst_blas_descs + 0, 16 };
	desc.resultSizeArray         = { dst_blas_descs + 8, 16 };
	desc.indirectArgArray        = { indirect_arguments, sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS) };
	desc.indirectArgCount        = ComputeGpuVirtualAddress(inputs.indirect_argument_count, resources);
	
	NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS params = {};
	params.version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER;
	params.pDesc   = &desc;
	
	auto result = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(command_list, &params);
	DebugAssert(result == NVAPI_OK, "NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation failed.");
}

static void CmdBuildTlasD3D12(CmdBuildTlasPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	ProfilerScope("CmdBuildTlasD3D12", command_list);
	auto& inputs = packet->inputs;
	
	NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX desc = {};
	desc.destAccelerationStructureData    = ComputeGpuVirtualAddress(inputs.result_tlas, resources);
	desc.inputs                           = TranslateBuildLimitsTLAS(inputs.limits);
	desc.inputs.instanceDescs             = ComputeGpuVirtualAddress(inputs.instance_descs, resources);
	desc.sourceAccelerationStructureData  = 0;
	desc.scratchAccelerationStructureData = ComputeGpuVirtualAddress(inputs.scratch_data, resources);
	
	NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS params = {};
	params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;
	params.pDesc   = &desc;
	
	auto result = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(command_list, &params);
	DebugAssert(result == NVAPI_OK, "NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation failed.");
}

static void CmdCopyBufferToTextureD3D12(CmdCopyBufferToTexturePacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	auto& src_resource = resources[(u32)packet->src_buffer_gpu_address.resource_id];
	auto& dst_resource = resources[(u32)packet->dst_texture_resource_id];
	
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource                          = src_resource.buffer.resource.d3d12;
	src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset             = packet->src_buffer_gpu_address.offset;
	src.PlacedFootprint.Footprint.Format   = dxgi_texture_format_map[(u32)dst_resource.texture.size.format];
	src.PlacedFootprint.Footprint.Width    = packet->src_size.x;
	src.PlacedFootprint.Footprint.Height   = packet->src_size.y;
	src.PlacedFootprint.Footprint.Depth    = packet->src_size.z;
	src.PlacedFootprint.Footprint.RowPitch = packet->src_row_pitch;
	
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource        = dst_resource.texture.resource.d3d12;
	dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = packet->dst_subresource_index;
	
	command_list->CopyTextureRegion(&dst, packet->dst_offset.x, packet->dst_offset.y, packet->dst_offset.z, &src, nullptr);
}

static void CmdCopyBufferToBufferD3D12(CmdCopyBufferToBufferPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	auto& src_resource = resources[(u32)packet->src_gpu_address.resource_id];
	auto& dst_resource = resources[(u32)packet->dst_gpu_address.resource_id];
	
	command_list->CopyBufferRegion(dst_resource.buffer.resource.d3d12, packet->dst_gpu_address.offset, src_resource.buffer.resource.d3d12, packet->src_gpu_address.offset, packet->size);
}

static void CmdSetRootSignatureD3D12(CmdSetRootSignaturePacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context) {
	if (packet->pass_type == CommandQueueType::Compute) {
		command_list->SetComputeRootSignature(context->root_signature_table[packet->root_signature_id.index]);
	} else {
		command_list->SetGraphicsRootSignature(context->root_signature_table[packet->root_signature_id.index]);
	}
}

static void CmdSetPipelineStateD3D12(CmdSetPipelineStatePacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context) {
	command_list->SetPipelineState(context->pipeline_state_table[packet->pipeline_id.index]);
}

static void CmdSetDescriptorTableD3D12(CmdSetDescriptorTablePacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context) {
	auto gpu_base_handle = context->gpu_base_handles[(u32)DescriptorHeapType::SRV];
	auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
	u64 descriptor_table = gpu_base_handle.ptr + packet->descriptor_heap_offset * descriptor_size;
	
	if (packet->pass_type == CommandQueueType::Compute) {
		command_list->SetComputeRootDescriptorTable(packet->offset, { descriptor_table });
	} else {
		command_list->SetGraphicsRootDescriptorTable(packet->offset, { descriptor_table });
	}
}

static void CmdSetPushConstantsD3D12(CmdSetPushConstantsPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	if (packet->pass_type == CommandQueueType::Compute) {
		command_list->SetComputeRoot32BitConstants(packet->offset, (u32)packet->push_constants.count, packet->push_constants.data, 0);
	} else {
		command_list->SetGraphicsRoot32BitConstants(packet->offset, (u32)packet->push_constants.count, packet->push_constants.data, 0);
	}
}

static void CmdSetConstantBufferD3D12(CmdSetConstantBufferPacket* packet, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	u64 gpu_address = ComputeGpuVirtualAddress(packet->gpu_address, resources);
	
	if (packet->pass_type == CommandQueueType::Compute) {
		command_list->SetComputeRootConstantBufferView(packet->offset, gpu_address);
	} else {
		command_list->SetGraphicsRootConstantBufferView(packet->offset, gpu_address);
	}
}

static void CmdBeginProfilerScopeD3D12(CmdBeginProfilerScopePacket* packet, ID3D12GraphicsCommandList7* command_list) {
	ProfilerBeginScope(packet->label.data, command_list);
}

static void CmdEndProfilerScopeD3D12(CmdEndProfilerScopePacket* packet, ID3D12GraphicsCommandList7* command_list) {
	ProfilerEndScope(command_list);
}

static void CmdDispatchXessD3D12(CmdDispatchXessPacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context, ArrayView<VirtualResource> resources) {
	ProfilerScope("CmdDispatchXessD3D12", command_list);
	
	auto& xess_handle_resource   = resources[(u32)packet->xess_handle_resource_id];
	auto& result_resource        = resources[(u32)packet->result_resource_id];
	auto& radiance_resource      = resources[(u32)packet->radiance_resource_id];
	auto& depth_resource         = resources[(u32)packet->depth_resource_id];
	auto& motion_vector_resource = resources[(u32)packet->motion_vector_resource_id];
	
	auto src_size = uint2((u32)xess_handle_resource.opaque.user_data_1, (u32)(xess_handle_resource.opaque.user_data_1 >> 32));
	
	bool is_dirty = (xess_handle_resource.type != VirtualResource::Type::Opaque) ||
		(src_size.x != depth_resource.texture.size.x) ||
		(src_size.y != depth_resource.texture.size.y);
	
	if (is_dirty) {
		ProfilerScope("xessD3D12CreateContext/xessD3D12Init");
		
		if (xess_handle_resource.type == VirtualResource::Type::Opaque) {
			xess_handle_resource.opaque.release_user_data(&xess_handle_resource, context);
		}
		
		xess_context_handle_t xess_context = nullptr;
		auto result = xessD3D12CreateContext(context->device, &xess_context);
		DebugAssert(result == XESS_RESULT_SUCCESS, "xessD3D12CreateContext failed.");
		
		src_size = uint2(depth_resource.texture.size);
		
		xess_handle_resource.type = VirtualResource::Type::Opaque;
		xess_handle_resource.opaque.user_data_0 = xess_context;
		xess_handle_resource.opaque.user_data_1 = (u64)src_size.x | ((u64)src_size.y << 32);
		xess_handle_resource.opaque.release_user_data = [](VirtualResource* xess_handle_resource, GraphicsContext*) {
			ProfilerScope("xessDestroyContext");
			auto xess_context = (xess_context_handle_t)xess_handle_resource->opaque.user_data_0;
			
			// TODO: Defer destroy.
			auto result = xessDestroyContext(xess_context);
			DebugAssert(result == XESS_RESULT_SUCCESS, "xessDestroyContext failed.");
			
			*xess_handle_resource = {};
		};
		
		xess_d3d12_init_params_t init_params = {};
		init_params.outputResolution.x = src_size.x;
		init_params.outputResolution.y = src_size.y;
		init_params.qualitySetting     = XESS_QUALITY_SETTING_AA;
		init_params.initFlags          = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE | XESS_INIT_FLAG_EXTERNAL_DESCRIPTOR_HEAP;
		result = xessD3D12Init(xess_context, &init_params);
		DebugAssert(result == XESS_RESULT_SUCCESS, "xessD3D12Init failed.");
		
		// UV to NDC transform.
		result = xessSetVelocityScale(xess_context, 2.f, -2.f);
		DebugAssert(result == XESS_RESULT_SUCCESS, "xessSetVelocityScale failed.");
	}
	
	auto xess_context = (xess_context_handle_t)xess_handle_resource.opaque.user_data_0;
	auto xess_output_resolution = xess_2d_t{ result_resource.texture.size.x, result_resource.texture.size.y };
	
	xess_properties_t xess_properties = {};
	auto result = xessGetProperties(xess_context, &xess_output_resolution, &xess_properties);
	DebugAssert(result == XESS_RESULT_SUCCESS, "xessGetProperties failed.");
	
	xess_d3d12_execute_params_t execute_params = {};
	execute_params.pColorTexture    = radiance_resource.texture.resource.d3d12;
	execute_params.pVelocityTexture = motion_vector_resource.texture.resource.d3d12;
	execute_params.pDepthTexture    = depth_resource.texture.resource.d3d12;
	execute_params.pOutputTexture   = result_resource.texture.resource.d3d12;
	execute_params.jitterOffsetX    = packet->jitter_offset_pixels.x;
	execute_params.jitterOffsetY    = packet->jitter_offset_pixels.y;
	execute_params.inputWidth       = src_size.x;
	execute_params.inputHeight      = src_size.y;
	execute_params.exposureScale    = 1.f;
	execute_params.pDescriptorHeap  = context->descriptor_heaps[(u32)DescriptorHeapType::SRV];
	execute_params.descriptorHeapOffset = AllocateTransientSrvDescriptorTable(context, xess_properties.requiredDescriptorCount) * context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
	
	result = xessD3D12Execute(xess_context, command_list, &execute_params);
	DebugAssert(result == XESS_RESULT_SUCCESS, "xessD3D12Execute failed.");
	
	command_list->SetDescriptorHeaps(1, &context->descriptor_heaps[(u32)DescriptorHeapType::SRV]);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void CmdDispatchDlssD3D12(CmdDispatchDlssPacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context, ArrayView<VirtualResource> resources) {
	ProfilerScope("CmdDispatchDlssD3D12", command_list);
	
	auto& dlss_handle_resource   = resources[(u32)packet->dlss_handle_resource_id];
	auto& result_resource        = resources[(u32)packet->result_resource_id];
	auto& radiance_resource      = resources[(u32)packet->radiance_resource_id];
	auto& depth_resource         = resources[(u32)packet->depth_resource_id];
	auto& motion_vector_resource = resources[(u32)packet->motion_vector_resource_id];
	
	auto src_size = uint2((u32)dlss_handle_resource.opaque.user_data_1, (u32)(dlss_handle_resource.opaque.user_data_1 >> 32));
	
	bool is_dirty = (dlss_handle_resource.type != VirtualResource::Type::Opaque) ||
		(src_size.x != depth_resource.texture.size.x) ||
		(src_size.y != depth_resource.texture.size.y);
	
	compile_const char* ngx_parameter_user_dlss_handle = "UserDlssHandle";
	
	if (is_dirty) {
		ProfilerScope("NVSDK_NGX_D3D12_AllocateParameters/NGX_D3D12_CREATE_DLSS_EXT");
		
		if (dlss_handle_resource.type == VirtualResource::Type::Opaque) {
			dlss_handle_resource.opaque.release_user_data(&dlss_handle_resource, context);
		}
		
		src_size = uint2(depth_resource.texture.size);
		
		NVSDK_NGX_Parameter* dlss_parameter_handle = nullptr;
		auto result = NVSDK_NGX_D3D12_AllocateParameters(&dlss_parameter_handle);
		DebugAssert(result == NVSDK_NGX_Result_Success, "xessD3D12Init failed.");
		
		NVSDK_NGX_DLSS_Create_Params create_params = {};
		create_params.Feature.InWidth        = src_size.x;
		create_params.Feature.InHeight       = src_size.y;
		create_params.Feature.InTargetWidth  = src_size.x;
		create_params.Feature.InTargetHeight = src_size.y;
		create_params.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
		create_params.InFeatureCreateFlags   = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		create_params.InEnableOutputSubrects = false;
		
		NVSDK_NGX_Handle* dlss_handle = nullptr;
		result = NGX_D3D12_CREATE_DLSS_EXT(command_list, 0, 0, &dlss_handle, dlss_parameter_handle, &create_params);
		DebugAssert(result == NVSDK_NGX_Result_Success, "NGX_D3D12_CREATE_DLSS_EXT failed.");
		
		NVSDK_NGX_Parameter_SetVoidPointer(dlss_parameter_handle, ngx_parameter_user_dlss_handle, dlss_handle);
		
		dlss_handle_resource.type = VirtualResource::Type::Opaque;
		dlss_handle_resource.opaque.user_data_0 = dlss_parameter_handle;
		dlss_handle_resource.opaque.user_data_1 = (u64)src_size.x | ((u64)src_size.y << 32);
		dlss_handle_resource.opaque.release_user_data = [](VirtualResource* dlss_handle_resource, GraphicsContext*) {
			ProfilerScope("NVSDK_NGX_D3D12_ReleaseFeature/NVSDK_NGX_D3D12_DestroyParameters");
			auto* dlss_parameter_handle = (NVSDK_NGX_Parameter*)dlss_handle_resource->opaque.user_data_0;
			
			// TODO: Defer destroy.
			NVSDK_NGX_Handle* dlss_handle = nullptr; 
			NVSDK_NGX_Parameter_GetVoidPointer(dlss_parameter_handle, ngx_parameter_user_dlss_handle, (void**)&dlss_handle);
			
			auto result = NVSDK_NGX_D3D12_ReleaseFeature(dlss_handle);
			DebugAssert(result == NVSDK_NGX_Result_Success, "NVSDK_NGX_D3D12_ReleaseFeature failed.");
			
			result = NVSDK_NGX_D3D12_DestroyParameters(dlss_parameter_handle);
			DebugAssert(result == NVSDK_NGX_Result_Success, "NVSDK_NGX_D3D12_DestroyParameters failed.");
			
			*dlss_handle_resource = {};
		};
	}
	
	auto* dlss_parameter_handle = (NVSDK_NGX_Parameter*)dlss_handle_resource.opaque.user_data_0;
	
	NVSDK_NGX_Handle* dlss_handle = nullptr; 
	NVSDK_NGX_Parameter_GetVoidPointer(dlss_parameter_handle, ngx_parameter_user_dlss_handle, (void**)&dlss_handle);
	
	NVSDK_NGX_D3D12_DLSS_Eval_Params eval_params = {};
	eval_params.Feature.pInColor  = radiance_resource.texture.resource.d3d12;
	eval_params.Feature.pInOutput = result_resource.texture.resource.d3d12;
	eval_params.Feature.InSharpness = 0.f;
	eval_params.pInDepth         = depth_resource.texture.resource.d3d12;
	eval_params.pInMotionVectors = motion_vector_resource.texture.resource.d3d12;
	eval_params.InJitterOffsetX  = packet->jitter_offset_pixels.x;
	eval_params.InJitterOffsetY  = packet->jitter_offset_pixels.y;
	eval_params.InMVScaleX       = (float)src_size.x;
	eval_params.InMVScaleY       = (float)src_size.y;
	eval_params.InPreExposure    = 1.f;
	eval_params.InExposureScale  = 1.f;
	eval_params.InRenderSubrectDimensions.Width  = src_size.x;
	eval_params.InRenderSubrectDimensions.Height = src_size.y;
	
	auto result = NGX_D3D12_EVALUATE_DLSS_EXT(command_list, dlss_handle, dlss_parameter_handle, &eval_params);
	DebugAssert(result == NVSDK_NGX_Result_Success, "NGX_D3D12_EVALUATE_DLSS_EXT failed.");
	
	command_list->SetDescriptorHeaps(1, &context->descriptor_heaps[(u32)DescriptorHeapType::SRV]);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void ResolveTextureAccess(D3D12_BARRIER_SYNC& sync, D3D12_BARRIER_ACCESS& access, D3D12_BARRIER_LAYOUT& layout, ResourceAccessDefinition* access_definition) {
	u32 access_mask = access_definition ? (u32)access_definition->access_mask : 0;
	u32 stages_mask = access_definition ? (u32)access_definition->stages_mask : 0;
	
	sync = D3D12_BARRIER_SYNC_NONE;
	if (stages_mask & (u32)PipelineStagesMask::ComputeShader) sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
	if (stages_mask & (u32)PipelineStagesMask::PixelShader)   sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
	if (stages_mask & (u32)PipelineStagesMask::VertexShader)  sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
	if (stages_mask & (u32)PipelineStagesMask::Copy)          sync |= D3D12_BARRIER_SYNC_COPY;
	if (stages_mask & (u32)PipelineStagesMask::RenderTarget)  sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
	if (stages_mask & (u32)PipelineStagesMask::DepthStencil)  sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
	
	if (access_mask & (u32)ResourceAccessMask::SRV) {
		access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		layout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		return;
	}
	
	if (access_mask & (u32)ResourceAccessMask::UAV) {
		access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		return;
	}
	
	if (access_mask & (u32)ResourceAccessMask::CopySrc) {
		access = D3D12_BARRIER_ACCESS_COPY_SOURCE;
		layout = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		return;
	}
	
	if (access_mask & (u32)ResourceAccessMask::CopyDst) {
		access = D3D12_BARRIER_ACCESS_COPY_DEST;
		layout = D3D12_BARRIER_LAYOUT_COPY_DEST;
		return;
	}
	
	if (access_mask & (u32)ResourceAccessMask::RenderTarget) {
		access = D3D12_BARRIER_ACCESS_RENDER_TARGET;
		layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		return;
	}
	
	if (access_mask & (u32)ResourceAccessMask::DepthStencilRW) {
		access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		return;
	}
	
	if (access_mask & (u32)ResourceAccessMask::DepthStencilRO) {
		access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		return;
	}
	
	access = D3D12_BARRIER_ACCESS_NO_ACCESS;
	layout = D3D12_BARRIER_LAYOUT_COMMON;
}

static void CreateTextureBarrier(Array<D3D12_TEXTURE_BARRIER>& barriers, StackAllocator* alloc, VirtualResource& resource, ResourceAccessDefinition* last_access, ResourceAccessDefinition* next_access) {
	bool is_full_resource_access = true;
	
	if (next_access) is_full_resource_access &= HasAnyFlags(next_access->flags, ResourceAccessFlags::IsFullResourceAccess);
	if (last_access) is_full_resource_access &= HasAnyFlags(last_access->flags, ResourceAccessFlags::IsFullResourceAccess);
	
	if (is_full_resource_access) {
		D3D12_TEXTURE_BARRIER barrier = {};
		ResolveTextureAccess(barrier.SyncBefore, barrier.AccessBefore, barrier.LayoutBefore, last_access);
		ResolveTextureAccess(barrier.SyncAfter,  barrier.AccessAfter,  barrier.LayoutAfter,  next_access);
		
		if (barrier.AccessBefore != barrier.AccessAfter || barrier.LayoutBefore != barrier.LayoutAfter) {
			barrier.pResource = resource.texture.resource.d3d12;
			barrier.Flags     = D3D12_TEXTURE_BARRIER_FLAG_NONE;
			
			auto* access = next_access ? next_access : last_access;
			barrier.Subresources.IndexOrFirstMipLevel = access->mip_index;
			barrier.Subresources.NumMipLevels         = access->mip_count;
			barrier.Subresources.FirstArraySlice      = access->array_index;
			barrier.Subresources.NumArraySlices       = access->array_count;
			barrier.Subresources.FirstPlane           = access->plane_mask == 0 ? 0 : FirstBitLow32(access->plane_mask);
			barrier.Subresources.NumPlanes            = access->plane_mask == 0 ? 1 : CountSetBits32(access->plane_mask);
			ArrayAppend(barriers, alloc, barrier);
		}
	} else {
		u32 begin_mip_index   = next_access ? next_access->mip_index   : 0;
		u32 begin_array_index = next_access ? next_access->array_index : 0;
		u32 end_mip_index     = next_access ? begin_mip_index   + next_access->mip_count   : resource.texture.size.mips;
		u32 end_array_index   = next_access ? begin_array_index + next_access->array_count : resource.texture.size.ArraySliceCount();
		
		for (u32 mip_index = begin_mip_index; mip_index < end_mip_index; mip_index += 1) {
			for (u32 array_index = begin_array_index; array_index < end_array_index;) {
				u32 common_array_count = 1;
				
				auto* last_subresource_access = last_access;
				while (last_subresource_access != nullptr) {
					bool mip_index_matches   = last_subresource_access->mip_index   <= mip_index   && mip_index   < ((u32)last_subresource_access->mip_index   + last_subresource_access->mip_count);
					bool array_index_matches = last_subresource_access->array_index <= array_index && array_index < ((u32)last_subresource_access->array_index + last_subresource_access->array_count);
					
					if (mip_index_matches && array_index_matches) {
						common_array_count = Math::Min((u32)last_subresource_access->array_index + last_subresource_access->array_count, end_array_index) - array_index;
						break;
					}
					
					last_subresource_access = last_subresource_access->last_access;
				}
				
				D3D12_TEXTURE_BARRIER barrier = {};
				ResolveTextureAccess(barrier.SyncBefore, barrier.AccessBefore, barrier.LayoutBefore, last_subresource_access);
				ResolveTextureAccess(barrier.SyncAfter,  barrier.AccessAfter,  barrier.LayoutAfter,  next_access);
				
				if (barrier.AccessBefore != barrier.AccessAfter || barrier.LayoutBefore != barrier.LayoutAfter) {
					barrier.pResource = resource.texture.resource.d3d12;
					barrier.Flags     = D3D12_TEXTURE_BARRIER_FLAG_NONE;
					
					auto* access = next_access ? next_access : last_subresource_access;
					barrier.Subresources.IndexOrFirstMipLevel = mip_index;
					barrier.Subresources.NumMipLevels         = 1;
					barrier.Subresources.FirstArraySlice      = array_index;
					barrier.Subresources.NumArraySlices       = common_array_count;
					barrier.Subresources.FirstPlane           = access->plane_mask == 0 ? 0 : FirstBitLow32(access->plane_mask);
					barrier.Subresources.NumPlanes            = access->plane_mask == 0 ? 1 : CountSetBits32(access->plane_mask);
					ArrayAppend(barriers, alloc, barrier);
				}
				
				array_index += common_array_count;
			}
		}
	}
}

static void ResolveBufferAccess(D3D12_BARRIER_SYNC& sync, D3D12_BARRIER_ACCESS& access, ResourceAccessDefinition* access_definition) {
	DebugAssert(access_definition != nullptr, "Buffers don't need to sync across command lists.");
	
	u32 access_mask = (u32)access_definition->access_mask;
	u32 stages_mask = (u32)access_definition->stages_mask;
	
	sync = D3D12_BARRIER_SYNC_NONE;
	if (stages_mask & (u32)PipelineStagesMask::ComputeShader) sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
	if (stages_mask & (u32)PipelineStagesMask::PixelShader)   sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
	if (stages_mask & (u32)PipelineStagesMask::VertexShader)  sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
	if (stages_mask & (u32)PipelineStagesMask::Copy)          sync |= D3D12_BARRIER_SYNC_COPY;
	if (stages_mask & (u32)PipelineStagesMask::IndirectArguments) sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
	if (stages_mask & (u32)PipelineStagesMask::RtasBuild)     sync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
	
	access = D3D12_BARRIER_ACCESS_COMMON;
	if (access_mask & (u32)ResourceAccessMask::SRV)     access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	if (access_mask & (u32)ResourceAccessMask::UAV)     access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	if (access_mask & (u32)ResourceAccessMask::CopySrc) access |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
	if (access_mask & (u32)ResourceAccessMask::CopyDst) access |= D3D12_BARRIER_ACCESS_COPY_DEST;
	if (access_mask & (u32)ResourceAccessMask::IndirectArguments) access |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
	if (access_mask & (u32)ResourceAccessMask::RtasRO)  access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
	if (access_mask & (u32)ResourceAccessMask::RtasRW)  access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
}

static void CreateBufferBarrier(Array<D3D12_BUFFER_BARRIER>& barriers, VirtualResource& resource, ResourceAccessDefinition* last_access, ResourceAccessDefinition* next_access) {
	if (last_access == nullptr || next_access == nullptr) return;
	
	D3D12_BUFFER_BARRIER barrier = {};
	ResolveBufferAccess(barrier.SyncBefore, barrier.AccessBefore, last_access);
	ResolveBufferAccess(barrier.SyncAfter,  barrier.AccessAfter,  next_access);
	
	if ((barrier.AccessBefore != barrier.AccessAfter) || ((barrier.AccessBefore & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) && (barrier.AccessAfter & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS))) {
		barrier.pResource = resource.texture.resource.d3d12;
		barrier.Offset    = 0;
		barrier.Size      = u64_max;
		ArrayAppend(barriers, barrier);
	}
}

static void CmdBarriersD3D12(ArrayView<D3D12_TEXTURE_BARRIER> texture_barriers, ArrayView<D3D12_BUFFER_BARRIER> buffer_barriers, ID3D12GraphicsCommandList7* command_list) {
	FixedCapacityArray<D3D12_BARRIER_GROUP, 2> barrier_groups;
	
	if (texture_barriers.count != 0) {
		D3D12_BARRIER_GROUP barrier_group = {};
		barrier_group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
		barrier_group.NumBarriers      = (u32)texture_barriers.count;
		barrier_group.pTextureBarriers = texture_barriers.data;
		ArrayAppend(barrier_groups, barrier_group);
	}
	
	if (buffer_barriers.count != 0) {
		D3D12_BARRIER_GROUP barrier_group = {};
		barrier_group.Type            = D3D12_BARRIER_TYPE_BUFFER;
		barrier_group.NumBarriers     = (u32)buffer_barriers.count;
		barrier_group.pBufferBarriers = buffer_barriers.data;
		ArrayAppend(barrier_groups, barrier_group);
	}
	
	if (barrier_groups.count != 0) {
		command_list->Barrier((u32)barrier_groups.count, barrier_groups.data);
	}
}

static void CmdBarriersD3D12(StackAllocator* alloc, ArrayView<ResourceAccessDefinition> resource_accesses, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources) {
	TempAllocationScope(alloc);
	
	Array<D3D12_TEXTURE_BARRIER> texture_barriers;
	Array<D3D12_BUFFER_BARRIER> buffer_barriers;
	ArrayReserve(texture_barriers, alloc, resource_accesses.count);
	ArrayReserve(buffer_barriers,  alloc, resource_accesses.count);
	
	for (auto& access : resource_accesses) {
		auto& resource = resources[(u32)access.resource_id];
		if (access.last_access == nullptr) continue;
		
		if (HasAnyFlags(access.flags, ResourceAccessFlags::IsTexture)) {
			CreateTextureBarrier(texture_barriers, alloc, resource, access.last_access, &access);
		} else {
			CreateBufferBarrier(buffer_barriers, resource, access.last_access, &access);
		}
	}
	
	CmdBarriersD3D12(texture_barriers, buffer_barriers, command_list);
}

static void CmdBarriersD3D12(StackAllocator* alloc, ArrayView<ResourceAccessDefinition*> resource_accesses, ID3D12GraphicsCommandList7* command_list, ArrayView<VirtualResource> resources, bool is_first_access) {
	TempAllocationScope(alloc);
	
	Array<D3D12_TEXTURE_BARRIER> texture_barriers;
	Array<D3D12_BUFFER_BARRIER> buffer_barriers;
	ArrayReserve(texture_barriers, alloc, resource_accesses.count);
	ArrayReserve(buffer_barriers,  alloc, resource_accesses.count);
	
	for (u32 resource_index = 0; resource_index < resource_accesses.count; resource_index += 1) {
		auto* access = resource_accesses[resource_index];
		if (access == nullptr) continue;
		
		auto* last_access = is_first_access ? nullptr : access;
		auto* next_access = is_first_access ? access : nullptr;
		
		if (HasAnyFlags(access->flags, ResourceAccessFlags::IsTexture)) {
			CreateTextureBarrier(texture_barriers, alloc, resources[resource_index], last_access, next_access);
		} else {
			CreateBufferBarrier(buffer_barriers, resources[resource_index], last_access, next_access);
		}
	}
	
	CmdBarriersD3D12(texture_barriers, buffer_barriers, command_list);
}

static bool IsReadOnly(ResourceAccessMask access_mask) {
	return HasAnyFlags(access_mask, ResourceAccessMask::AccessRW) == false;
}

static bool AccessRangesAreEqual(ResourceAccessDefinition* access_0, ResourceAccessDefinition* access_1) {
	return
		access_0->mip_index   == access_1->mip_index   && access_0->mip_count   == access_1->mip_count &&
		access_0->array_index == access_1->array_index && access_0->array_count == access_1->array_count;
}

struct ResourceAccesses {
	ArrayView<ResourceAccessDefinition*> first_resource_access;
	ArrayView<ResourceAccessDefinition*> last_resource_access;
};

static ResourceAccesses ResolveResourceAccesses(StackAllocator* alloc, ArrayView<ArrayView<ResourceAccessDefinition>> resource_accesses, ArrayView<VirtualResource> resources) {
	Array<ResourceAccessDefinition*> first_resource_access;
	Array<ResourceAccessDefinition*> last_resource_access;
	ArrayResizeMemset(first_resource_access, alloc, resources.count);
	ArrayResizeMemset(last_resource_access,  alloc, resources.count);
	
	for (auto& accesses : resource_accesses) {
		for (auto& access : accesses) {
			u32 resource_index = (u32)access.resource_id;
			
			auto* last_access = last_resource_access[resource_index];
			if (last_access == nullptr) {
				first_resource_access[resource_index] = &access;
			}
			
			bool erase_access = false;
			if (HasAnyFlags(access.flags, ResourceAccessFlags::IsTexture)) {
				auto& resource = resources[resource_index];
				
				// Upstream code might set counts to u8_max/u16_max to signal all remaining mip/array slices.
				// Clamp access ranges so any further code can assume they're correct.
				access.mip_count   = Math::Min(access.mip_count,   (u8)(resource.texture.size.mips               - access.mip_index));
				access.array_count = Math::Min(access.array_count, (u16)(resource.texture.size.ArraySliceCount() - access.array_index));
				
				if (access.mip_count == resource.texture.size.mips && access.array_count && resource.texture.size.ArraySliceCount()) {
					access.flags |= ResourceAccessFlags::IsFullResourceAccess;
				}
				
				bool is_same_group  = accesses.begin() <= last_access && last_access < accesses.end();
				bool is_same_access_and_stages = is_same_group && (last_access->stages_mask == access.stages_mask) && (last_access->access_mask == access.access_mask);
				bool is_next_mip = is_same_group && (last_access->mip_index + last_access->mip_count) == access.mip_index && (last_access->array_index == access.array_index && last_access->array_count == access.array_count);
				
				if (is_same_group && is_same_access_and_stages && is_next_mip) {
					last_access->mip_count += access.mip_count;
					
					if (last_access->mip_count == resource.texture.size.mips && last_access->array_count && resource.texture.size.ArraySliceCount()) {
						last_access->flags |= ResourceAccessFlags::IsFullResourceAccess;
					}
					
					erase_access = true;
				} else if (last_access && IsReadOnly(last_access->access_mask) && IsReadOnly(access.access_mask) && AccessRangesAreEqual(last_access, &access)) {
					// Fold all read only accesses together into the earliest access.
					last_access->stages_mask |= access.stages_mask;
					last_access->access_mask |= access.access_mask;
					erase_access = true;
				}
			} else {
				if (accesses.begin() <= last_access && last_access < accesses.end()) {
					// Fold current access into the last access of the same group.
					last_access->stages_mask |= access.stages_mask;
					last_access->access_mask |= access.access_mask;
					erase_access = true;
				} else if (last_access && IsReadOnly(last_access->access_mask) && IsReadOnly(access.access_mask)) {
					// Fold all read only accesses together into the earliest access.
					last_access->stages_mask |= access.stages_mask;
					last_access->access_mask |= access.access_mask;
					erase_access = true;
				}
			}
			
			if (erase_access) {
				// If we call ArrayEraseSwapLast here, texture subresource access merging won't work correctly. Defer erase to the next loop.
				access.flags |= ResourceAccessFlags::IsErased;
			} else {
				last_resource_access[resource_index] = &access;
				access.last_access = last_access;
			}
		}
		
		for (u32 i = 0; i < accesses.count;) {
			if (HasAnyFlags(accesses[i].flags, ResourceAccessFlags::IsErased)) {
				ArrayEraseSwapLast(accesses, i);
			} else {
				i += 1;
			}
		}
	}
	
	return { first_resource_access, last_resource_access };
}

void ReplayRecordContext(GraphicsContext* api_context, RecordContext* record_context) {
	ProfilerScope("ReplayRecordContext");
	
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto* alloc = record_context->alloc;
	TempAllocationScope(alloc);
	
	auto resources = ArrayView<VirtualResource>(record_context->resource_table->virtual_resources);
	
	for (auto& resource : resources) {
		if (resource.type == VirtualResource::Type::VirtualTexture && resource.texture.size != resource.texture.allocated_size) {
			if (resource.texture.resource.handle != nullptr) {
				ReleaseTextureResource(context, resource.texture.resource, ResourceReleaseCondition::EndOfThisGpuFrame);
			}
			
			resource.texture.resource = CreateTextureResource(context, resource.texture.size, resource.flags);
			resource.texture.allocated_size = resource.texture.size;
		} else if (resource.type == VirtualResource::Type::VirtualBuffer && resource.buffer.size != resource.buffer.allocated_size) {
			if (resource.buffer.resource.handle != nullptr) {
				ReleaseBufferResource(context, resource.buffer.resource, ResourceReleaseCondition::EndOfThisGpuFrame);
			}
			
			resource.buffer.resource = CreateBufferResource(context, resource.buffer.size, resource.flags);
			resource.buffer.allocated_size = resource.buffer.size;
		}
	}
	
	CreateDescriptorTables(context, record_context->descriptor_tables, resources);
	
	auto [first_resource_access, last_resource_access] = ResolveResourceAccesses(alloc, record_context->resource_accesses, resources);
	
	auto* command_list = context->graphics_context.command_list;
	
	u32 command_count  = record_context->command_count;
	u8* command_memory = record_context->command_memory_base;
	
	auto command_prefix_sum = ArrayView<u32>(record_context->resource_access_command_prefix_sum);
	auto resource_accesses  = record_context->resource_accesses;
	
	CmdBarriersD3D12(alloc, first_resource_access, command_list, resources, true);
	
	u32 begin_command_index = 0;
	for (u64 i = 0; i < command_prefix_sum.count; i += 1) {
		u32 end_command_index = command_prefix_sum[i];
		
		CmdBarriersD3D12(alloc, resource_accesses[i], command_list, resources);
		
		for (u32 command_index = begin_command_index; command_index < end_command_index; command_index += 1) {
			auto* packet = (RecordContextCommandPacket*)command_memory;
			command_memory += packet->packet_size;
			
			switch (packet->packet_type) {
			case CommandType::Jump: command_memory = ((CmdJumpPacket*)packet)->command_memory; break;
			case CommandType::Dispatch:              CmdDispatchD3D12((CmdDispatchPacket*)packet, command_list); break;
			case CommandType::DispatchMesh:          CmdDispatchMeshD3D12((CmdDispatchMeshPacket*)packet, command_list); break;
			case CommandType::DrawInstanced:         CmdDrawInstancedD3D12((CmdDrawInstancedPacket*)packet, command_list); break;
			case CommandType::DrawIndexedInstanced:  CmdDrawIndexedInstancedD3D12((CmdDrawIndexedInstancedPacket*)packet, command_list); break;
			case CommandType::ExecuteIndirect:       CmdExecuteIndirectD3D12((CmdExecuteIndirectPacket*)packet, command_list, context, resources); break;
			case CommandType::BuildMeshletRTAS:      CmdBuildMeshletRtasD3D12((CmdBuildMeshletRtasPacket*)packet, command_list, resources); break;
			case CommandType::MoveMeshletRTAS:       CmdMoveMeshletRtasD3D12((CmdMoveMeshletRtasPacket*)packet, command_list, resources); break;
			case CommandType::BuildMeshletBLAS:      CmdBuildMeshletBlasD3D12((CmdBuildMeshletBlasPacket*)packet, command_list, resources); break;
			case CommandType::BuildTLAS:             CmdBuildTlasD3D12((CmdBuildTlasPacket*)packet, command_list, resources); break;
			case CommandType::CopyBufferToTexture:   CmdCopyBufferToTextureD3D12((CmdCopyBufferToTexturePacket*)packet, command_list, resources); break;
			case CommandType::CopyBufferToBuffer:    CmdCopyBufferToBufferD3D12((CmdCopyBufferToBufferPacket*)packet, command_list, resources); break;
			case CommandType::ClearRenderTarget:     CmdClearRenderTargetD3D12((CmdClearRenderTargetPacket*)packet, command_list, context, resources); break;
			case CommandType::ClearDepthStencil:     CmdClearDepthStencilD3D12((CmdClearDepthStencilPacket*)packet, command_list, context, resources); break;
			case CommandType::SetRenderTargets:      CmdSetRenderTargetsD3D12((CmdSetRenderTargetsPacket*)packet, command_list, context, resources); break;
			case CommandType::SetViewport:           CmdSetViewportD3D12((CmdSetViewportAndScissorPacket*)packet, command_list); break;
			case CommandType::SetScissor:            CmdSetScissorD3D12((CmdSetViewportAndScissorPacket*)packet, command_list); break;
			case CommandType::SetIndexBufferView:    CmdSetIndexBufferViewD3D12((CmdSetIndexBufferViewPacket*)packet, command_list, resources); break;
			case CommandType::SetRootSignature:      CmdSetRootSignatureD3D12((CmdSetRootSignaturePacket*)packet, command_list, context); break;
			case CommandType::SetPipelineState:      CmdSetPipelineStateD3D12((CmdSetPipelineStatePacket*)packet, command_list, context); break;
			case CommandType::SetDescriptorTable:    CmdSetDescriptorTableD3D12((CmdSetDescriptorTablePacket*)packet, command_list, context); break;
			case CommandType::SetPushConstants:      CmdSetPushConstantsD3D12((CmdSetPushConstantsPacket*)packet, command_list); break;
			case CommandType::SetConstantBuffer:     CmdSetConstantBufferD3D12((CmdSetConstantBufferPacket*)packet, command_list, resources); break;
			case CommandType::BeginProfilerScope:    CmdBeginProfilerScopeD3D12((CmdBeginProfilerScopePacket*)packet, command_list); break;
			case CommandType::EndProfilerScope:      CmdEndProfilerScopeD3D12((CmdEndProfilerScopePacket*)packet, command_list); break;
			case CommandType::DispatchXeSS:          CmdDispatchXessD3D12((CmdDispatchXessPacket*)packet, command_list, context, resources); break;
			case CommandType::DispatchDLSS:          CmdDispatchDlssD3D12((CmdDispatchDlssPacket*)packet, command_list, context, resources); break;
			default: DebugAssertAlways("Unhandled command packet type '%'.", (u32)packet->packet_type); command_index = command_count; break;
			}
		}
		
		begin_command_index = end_command_index;
	}
	
	CmdBarriersD3D12(alloc, last_resource_access, command_list, resources, false);
}
