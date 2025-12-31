#include "GraphicsApiD3D12.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicMath.h"
#include "RecordContext.h"
#include "RecordContextCommands.h"


static void CreateDescriptorTables(GraphicsContextD3D12* context, ArrayView<HLSL::BaseDescriptorTable*> descriptor_tables, ArrayView<VirtualResource> resources) {
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
				desc.Format                        = dxgi_texture_format_map[(u32)resource.texture.size.format];
				desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.Texture2D.MostDetailedMip     = descriptor.texture.mip_index;
				desc.Texture2D.MipLevels           = Min(descriptor.texture.mip_count, resource.texture.size.mips - descriptor.texture.mip_index);
				desc.Texture2D.PlaneSlice          = 0;
				desc.Texture2D.ResourceMinLODClamp = 0.f;
				
				device->CreateShaderResourceView(resource.texture.resource.d3d12, &desc, descriptor_table_handle);
				break;
			} case ResourceDescriptorType::RWTexture2D: {
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				desc.Format               = dxgi_texture_format_map[(u32)resource.texture.size.format];
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
				desc.Buffer.NumElements         = Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / descriptor.buffer.stride;
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
				desc.Buffer.NumElements          = Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / descriptor.buffer.stride;
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
				desc.Buffer.NumElements         = Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / sizeof(u32);
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
				desc.Buffer.NumElements          = Min(descriptor.buffer.size, resource.buffer.size - descriptor.buffer.offset) / sizeof(u32);
				desc.Buffer.StructureByteStride  = 0;
				desc.Buffer.CounterOffsetInBytes = 0;
				desc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;
				DebugAssert(descriptor.buffer.offset % 16 == 0, "RWByteBuffer offset is not correctly aligned.");
				
				device->CreateUnorderedAccessView(resource.buffer.resource.d3d12, nullptr, &desc, descriptor_table_handle);
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
	auto& resource = resources[(u32)packet->gpu_address.resource_id];
	
	D3D12_INDEX_BUFFER_VIEW index_buffer_view = {};
	index_buffer_view.BufferLocation = resource.buffer.resource.d3d12->GetGPUVirtualAddress() + packet->gpu_address.offset;
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
	u64 gpu_address = resources[(u32)packet->gpu_address.resource_id].buffer.resource.d3d12->GetGPUVirtualAddress() + packet->gpu_address.offset;
	
	if (packet->pass_type == CommandQueueType::Compute) {
		command_list->SetComputeRootConstantBufferView(packet->offset, gpu_address);
	} else {
		command_list->SetGraphicsRootConstantBufferView(packet->offset, gpu_address);
	}
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

static void CreateTextureBarrier(Array<D3D12_TEXTURE_BARRIER>& barriers, VirtualResource& resource, ResourceAccessDefinition* last_access, ResourceAccessDefinition* next_access) {
	D3D12_TEXTURE_BARRIER barrier = {};
	ResolveTextureAccess(barrier.SyncBefore, barrier.AccessBefore, barrier.LayoutBefore, last_access);
	ResolveTextureAccess(barrier.SyncAfter,  barrier.AccessAfter,  barrier.LayoutAfter,  next_access);
	
	if (barrier.AccessBefore != barrier.AccessAfter || barrier.LayoutBefore != barrier.LayoutAfter) {
		barrier.pResource = resource.texture.resource.d3d12;
		barrier.Flags     = D3D12_TEXTURE_BARRIER_FLAG_NONE;
		
		// TODO: What if mip/array ranges mismatch between last_access and next_access?
		auto* access = next_access ? next_access : last_access;
		barrier.Subresources.IndexOrFirstMipLevel = access->mip_index;
		barrier.Subresources.NumMipLevels         = Min(access->mip_count, resource.texture.size.mips - access->mip_index);
		barrier.Subresources.FirstArraySlice      = access->array_index;
		barrier.Subresources.NumArraySlices       = Min(access->array_count, (u16)resource.texture.size.ArraySliceCount() - access->array_index);
		
		if (access->plane_mask == 0) {
			barrier.Subresources.FirstPlane = 0;
			barrier.Subresources.NumPlanes  = 1;
		} else {
			barrier.Subresources.FirstPlane = FirstBitLow32(access->plane_mask);
			barrier.Subresources.NumPlanes  = CountSetBits32(access->plane_mask);
		}
		
		ArrayAppend(barriers, barrier);
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
	
	access = D3D12_BARRIER_ACCESS_COMMON;
	if (access_mask & (u32)ResourceAccessMask::SRV)     access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	if (access_mask & (u32)ResourceAccessMask::UAV)     access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	if (access_mask & (u32)ResourceAccessMask::CopySrc) access |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
	if (access_mask & (u32)ResourceAccessMask::CopyDst) access |= D3D12_BARRIER_ACCESS_COPY_DEST;
	if (access_mask & (u32)ResourceAccessMask::IndirectArguments) access |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
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
	ArrayReserve(buffer_barriers, alloc, resource_accesses.count);
	
	for (auto& access : resource_accesses) {
		auto& resource = resources[(u32)access.resource_id];
		
		if (access.is_texture) {
			CreateTextureBarrier(texture_barriers, resource, access.last_access, &access);
		} else {
			CreateBufferBarrier(buffer_barriers, resource, access.last_access, &access);
		}
	}
	
	CmdBarriersD3D12(texture_barriers, buffer_barriers, command_list);
}

static bool IsReadOnly(ResourceAccessMask access_mask) {
	return HasAnyFlags(access_mask, ResourceAccessMask::AccessRW) == false;
}

static Array<ResourceAccessDefinition*> ResolveResourceAccesses(StackAllocator* alloc, ArrayView<ArrayView<ResourceAccessDefinition>> resource_accesses, ArrayView<VirtualResource> resources) {
	Array<ResourceAccessDefinition*> last_resource_access;
	ArrayResizeMemset(last_resource_access, alloc, resources.count);
	
	for (auto& accesses : resource_accesses) {
		for (u32 i = 0; i < accesses.count;) {
			auto& access = accesses[i];
			u32 resource_index = (u32)access.resource_id;
			
			auto* last_access = last_resource_access[resource_index];
			if (accesses.begin() <= last_access && last_access < accesses.end()) {
				DebugAssert(access.is_texture == false, "TODO: Texture resource is bound multiple times.");
				
				// Fold current access into the last access of the same group.
				last_access->stages_mask |= access.stages_mask;
				last_access->access_mask |= access.access_mask;
				
				ArrayEraseSwapLast(accesses, i);
			} else if (last_access && IsReadOnly(last_access->access_mask) && IsReadOnly(access.access_mask)) {
				// Fold all read only accesses together into the earliest access.
				last_access->stages_mask |= access.stages_mask;
				last_access->access_mask |= access.access_mask;
				
				ArrayEraseSwapLast(accesses, i);
			} else {
				last_resource_access[resource_index] = &access;
				i += 1;
				
				access.last_access = last_access;
			}
		}
	}
	
	return last_resource_access;
}

void ReplayRecordContext(GraphicsContext* api_context, RecordContext* record_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto* alloc = record_context->alloc;
	TempAllocationScope(alloc);
	
	auto resources = ArrayView<VirtualResource>(record_context->resource_table->virtual_resources);
	
	CreateDescriptorTables(context, record_context->descriptor_tables, resources);
	auto last_resource_access = ResolveResourceAccesses(alloc, record_context->resource_accesses, resources);
	
	auto* command_list = context->command_list;
	
	u32 command_count  = record_context->command_count;
	u8* command_memory = record_context->command_memory_base;
	
	auto command_prefix_sum = ArrayView<u32>(record_context->resource_access_command_prefix_sum);
	auto resource_accesses  = record_context->resource_accesses;
	
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
			default: DebugAssertAlways("Unhandled command packet type '%'.", (u32)packet->packet_type); command_index = command_count; break;
			}
		}
		
		begin_command_index = end_command_index;
	}
	
	{
		TempAllocationScope(alloc);
		
		Array<D3D12_TEXTURE_BARRIER> texture_barriers;
		Array<D3D12_BUFFER_BARRIER> buffer_barriers;
		ArrayReserve(texture_barriers, alloc, last_resource_access.count);
		ArrayReserve(buffer_barriers, alloc, last_resource_access.count);
		
		for (u32 resource_index = 0; resource_index < last_resource_access.count; resource_index += 1) {
			auto* access = last_resource_access[resource_index];
			if (access == nullptr) continue;
			
			if (access->is_texture) {
				CreateTextureBarrier(texture_barriers, resources[resource_index], access, nullptr);
			} else {
				CreateBufferBarrier(buffer_barriers, resources[resource_index], access, nullptr);
			}
		}
		
		CmdBarriersD3D12(texture_barriers, buffer_barriers, command_list);
	}
}
