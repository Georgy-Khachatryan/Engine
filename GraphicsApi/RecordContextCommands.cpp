#include "RecordContext.h"

#include "Basic/BasicMemory.h"
#include "RecordContextCommands.h"

compile_const u32 record_context_page_size = 4 * 1024;

template<typename CommandPacketT>
static CommandPacketT& AppendPacket(RecordContext* record_context) {
	if (record_context->remaining_size < sizeof(CmdJumpPacket) + sizeof(CommandPacketT)) {
		u8* new_command_memory = (u8*)record_context->alloc->Allocate(record_context_page_size);
		
		if (record_context->command_memory) {
			auto* jump = NewInPlace(record_context->command_memory, CmdJumpPacket);
			jump->packet_type = CmdJumpPacket::my_type;
			jump->packet_size = sizeof(CmdJumpPacket);
			jump->command_memory = new_command_memory;
			record_context->command_count += 1;
		} else {
			record_context->command_memory_base = new_command_memory;
		}
		
		record_context->command_memory = new_command_memory;
		record_context->remaining_size = record_context_page_size;
	}
	
	auto* packet = NewInPlace(record_context->command_memory, CommandPacketT);
	packet->packet_type = CommandPacketT::my_type;
	packet->packet_size = sizeof(CommandPacketT);
	
	record_context->command_memory += sizeof(CommandPacketT);
	record_context->remaining_size -= sizeof(CommandPacketT);
	record_context->command_count  += 1;
	
	return *packet;
}

static void AppendResourceAccesses(RecordContext* record_context, ArrayView<ResourceAccessDefinition> resource_accesses) {
	record_context->state_cache.is_dirty = true;

	ArrayAppend(record_context->resource_accesses, record_context->alloc, ArrayCopy(resource_accesses, record_context->alloc));
	ArrayAppend(record_context->resource_access_command_prefix_sum, record_context->alloc, record_context->command_count);
}

static ResourceAccessDefinition CreateBufferResourceAccess(VirtualResourceID resource_id, PipelineStagesMask stages_mask, ResourceAccessMask access_mask) {
	ResourceAccessDefinition access;
	access.resource_id = resource_id;
	access.is_texture  = false;
	access.stages_mask = stages_mask;
	access.access_mask = access_mask;
	return access;
}

static ResourceAccessDefinition CreateTextureResourceAccess(VirtualResourceID resource_id, PipelineStagesMask stages_mask, ResourceAccessMask access_mask, u8 plane_mask = 0) {
	ResourceAccessDefinition access;
	access.resource_id = resource_id;
	access.array_index = 0;
	access.array_count = 1;
	access.mip_index   = 0;
	access.mip_count   = 1;
	access.plane_mask  = plane_mask;
	access.is_texture  = true;
	access.stages_mask = stages_mask;
	access.access_mask = access_mask;
	return access;
}

static void AppendResourceBindings(RecordContext* record_context, ArrayView<ResourceAccessDefinition> indirect_accesses = {}) {
	auto& state_cache = record_context->state_cache;
	
	if (state_cache.is_dirty == false) {
		ArrayLastElement(record_context->resource_access_command_prefix_sum) = record_context->command_count;
		return;
	}
	state_cache.is_dirty = false;
	
	u32 accessed_resource_count = 0;
	for (auto* descriptor_table : state_cache.resource_bindings) {
		if (descriptor_table == nullptr) continue;
		accessed_resource_count += descriptor_table->descriptor_count;
	}
	
	if (HasAnyFlags(state_cache.stages_mask, PipelineStagesMask::RenderTarget)) {
		accessed_resource_count += (u32)state_cache.render_targets.count;
	}
	
	if (HasAnyFlags(state_cache.stages_mask, PipelineStagesMask::DepthStencil)) {
		accessed_resource_count += 2;
	}
	
	accessed_resource_count += (u32)indirect_accesses.count;
	
	Array<ResourceAccessDefinition> resource_accesses;
	ArrayReserve(resource_accesses, record_context->alloc, accessed_resource_count);
	
	auto shader_stages_mask = (state_cache.stages_mask & PipelineStagesMask::AnyShader);
	for (auto* descriptor_table : state_cache.resource_bindings) {
		if (descriptor_table == nullptr) continue;
		
		auto descriptors = ArrayView<ResourceDescriptor>{ (ResourceDescriptor*)(descriptor_table + 1), (u64)descriptor_table->descriptor_count };
		for (auto& descriptor : descriptors) {
			auto type = descriptor.common.type;
			if (HasAnyFlags(type, ResourceDescriptorType::AnyTexture)) {
				ResourceAccessDefinition access;
				access.resource_id = descriptor.resource_id;
				access.array_index = descriptor.texture.array_index;
				access.array_count = descriptor.texture.array_count;
				access.mip_index   = descriptor.texture.mip_index;
				access.mip_count   = descriptor.texture.mip_count;
				access.is_texture  = true;
				access.stages_mask = shader_stages_mask;
				access.access_mask = HasAnyFlags(type, ResourceDescriptorType::AnySRV) ? ResourceAccessMask::SRV : ResourceAccessMask::UAV;
				
				ArrayAppend(resource_accesses, access);
			} else if (HasAnyFlags(type, ResourceDescriptorType::AnyBuffer)) {
				ResourceAccessDefinition access;
				access.resource_id = descriptor.resource_id;
				access.is_texture  = false;
				access.stages_mask = shader_stages_mask;
				access.access_mask = HasAnyFlags(type, ResourceDescriptorType::AnySRV) ? ResourceAccessMask::SRV : ResourceAccessMask::UAV;
				
				ArrayAppend(resource_accesses, access);
			}
		}
	}
	
	if (HasAnyFlags(state_cache.stages_mask, PipelineStagesMask::RenderTarget)) {
		for (auto render_target : state_cache.render_targets) {
			ArrayAppend(resource_accesses, CreateTextureResourceAccess(render_target, PipelineStagesMask::RenderTarget, ResourceAccessMask::RenderTarget));
		}
	}
	
	if (HasAnyFlags(state_cache.stages_mask, PipelineStagesMask::DepthStencil)) {
		DebugAssert(state_cache.depth_stencil != (VirtualResourceID)0, "Pipeline accesses depth stencil buffer, but no depth stencil buffer is bound.");
		
		if (HasAnyFlags(state_cache.depth_stencil_access, DepthStencilAccess::DepthAccess)) {
			bool depth_write = HasAnyFlags(state_cache.depth_stencil_access, DepthStencilAccess::DepthWrite);
			auto stages_mask = depth_write ? PipelineStagesMask::DepthStencilRW : PipelineStagesMask::DepthStencilRO;
			auto access_mask = depth_write ? ResourceAccessMask::DepthStencilRW : ResourceAccessMask::DepthStencilRO;
			ArrayAppend(resource_accesses, CreateTextureResourceAccess(state_cache.depth_stencil, stages_mask, access_mask, 0x1));
		}
		
		if (HasAnyFlags(state_cache.depth_stencil_access, DepthStencilAccess::StencilAccess)) {
			bool stencil_write = HasAnyFlags(state_cache.depth_stencil_access, DepthStencilAccess::StencilWrite);
			auto stages_mask = stencil_write ? PipelineStagesMask::DepthStencilRW : PipelineStagesMask::DepthStencilRO;
			auto access_mask = stencil_write ? ResourceAccessMask::DepthStencilRW : ResourceAccessMask::DepthStencilRO;
			ArrayAppend(resource_accesses, CreateTextureResourceAccess(state_cache.depth_stencil, stages_mask, access_mask, 0x2));
		}
	}
	
	for (auto& access : indirect_accesses) {
		ArrayAppend(resource_accesses, access);
	}
	
	ArrayAppend(record_context->resource_accesses, record_context->alloc, resource_accesses);
	ArrayAppend(record_context->resource_access_command_prefix_sum, record_context->alloc, record_context->command_count);
}

void CmdDispatch(RecordContext* record_context, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	auto& packet = AppendPacket<CmdDispatchPacket>(record_context);
	packet.group_count = uint3(group_count_x, group_count_y, group_count_z);
	AppendResourceBindings(record_context);
}

void CmdDispatch(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z) {
	CmdDispatch(record_context, group_count_xy.x, group_count_xy.y, group_count_z);
}

void CmdDispatch(RecordContext* record_context, const uint3& group_count_xyz) {
	CmdDispatch(record_context, group_count_xyz.x, group_count_xyz.y, group_count_xyz.z);
}

void CmdDispatchMesh(RecordContext* record_context, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	auto& packet = AppendPacket<CmdDispatchMeshPacket>(record_context);
	packet.group_count = uint3(group_count_x, group_count_y, group_count_z);
	AppendResourceBindings(record_context);
}

void CmdDispatchMesh(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z) {
	CmdDispatchMesh(record_context, group_count_xy.x, group_count_xy.y, group_count_z);
}

void CmdDispatchMesh(RecordContext* record_context, const uint3& group_count_xyz) {
	CmdDispatchMesh(record_context, group_count_xyz.x, group_count_xyz.y, group_count_xyz.z);
}

void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location) {
	auto& packet = AppendPacket<CmdDrawInstancedPacket>(record_context);
	packet.vertex_count_per_instance = vertex_count_per_instance;
	packet.instance_count            = instance_count;
	packet.start_vertex_location     = start_vertex_location;
	packet.start_instance_location   = start_instance_location;
	AppendResourceBindings(record_context);
}

void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count, u32 start_index_location, u32 base_vertex_location, u32 start_instance_location) {
	auto& packet = AppendPacket<CmdDrawIndexedInstancedPacket>(record_context);
	packet.index_count_per_instance = index_count_per_instance;
	packet.instance_count           = instance_count;
	packet.start_index_location     = start_index_location;
	packet.base_vertex_location     = base_vertex_location;
	packet.start_instance_location  = start_instance_location;
	AppendResourceBindings(record_context);
}

static void CmdExecuteIndirect(RecordContext* record_context, GpuAddress indirect_arguments, CommandType indirect_command_type) {
	auto& packet = AppendPacket<CmdExecuteIndirectPacket>(record_context);
	packet.indirect_command_type = indirect_command_type;
	packet.indirect_arguments    = indirect_arguments;
	
	FixedCountArray<ResourceAccessDefinition, 1> resource_accesses;
	resource_accesses[0] = CreateBufferResourceAccess(indirect_arguments.resource_id, PipelineStagesMask::IndirectArguments, ResourceAccessMask::IndirectArguments);
	
	AppendResourceBindings(record_context, resource_accesses);
}

void CmdDispatchIndirect(RecordContext* record_context, GpuAddress indirect_arguments) {
	CmdExecuteIndirect(record_context, indirect_arguments, CommandType::Dispatch);
}

void CmdDispatchMeshIndirect(RecordContext* record_context, GpuAddress indirect_arguments) {
	CmdExecuteIndirect(record_context, indirect_arguments, CommandType::DispatchMesh);
}

void CmdDrawInstancedIndirect(RecordContext* record_context, GpuAddress indirect_arguments) {
	CmdExecuteIndirect(record_context, indirect_arguments, CommandType::DrawInstanced);
}

void CmdDrawIndexedInstancedIndirect(RecordContext* record_context, GpuAddress indirect_arguments) {
	CmdExecuteIndirect(record_context, indirect_arguments, CommandType::DrawIndexedInstanced);
}

void CmdCopyBufferToTexture(RecordContext* record_context, GpuAddress src_buffer_gpu_address, VirtualResourceID dst_texture_resource_id, u32 src_row_pitch, uint3 src_size, uint3 dst_offset, u32 dst_subresource_index) {
	auto& packet = AppendPacket<CmdCopyBufferToTexturePacket>(record_context);
	packet.src_buffer_gpu_address  = src_buffer_gpu_address;
	packet.dst_texture_resource_id = dst_texture_resource_id;
	packet.src_row_pitch           = src_row_pitch;
	packet.src_size                = src_size;
	packet.dst_subresource_index   = dst_subresource_index;
	packet.dst_offset              = dst_offset;
	
	DebugAssert(dst_subresource_index == 0, "TODO: Add support for non zero subresource index.");
	
	FixedCountArray<ResourceAccessDefinition, 2> resource_accesses;
	resource_accesses[0] = CreateBufferResourceAccess(src_buffer_gpu_address.resource_id, PipelineStagesMask::Copy, ResourceAccessMask::CopySrc);
	resource_accesses[1] = CreateTextureResourceAccess(dst_texture_resource_id, PipelineStagesMask::Copy, ResourceAccessMask::CopyDst);
	
	AppendResourceAccesses(record_context, resource_accesses);
}


void CmdClearRenderTarget(RecordContext* record_context, VirtualResourceID resource_id) {
	auto& packet = AppendPacket<CmdClearRenderTargetPacket>(record_context);
	packet.resource_id = resource_id;
	
	FixedCountArray<ResourceAccessDefinition, 1> resource_accesses;
	resource_accesses[0] = CreateTextureResourceAccess(resource_id, PipelineStagesMask::RenderTarget, ResourceAccessMask::RenderTarget);
	
	AppendResourceAccesses(record_context, resource_accesses);
}

void CmdClearDepthStencil(RecordContext* record_context, VirtualResourceID resource_id) {
	auto& packet = AppendPacket<CmdClearDepthStencilPacket>(record_context);
	packet.resource_id = resource_id;
	
	FixedCountArray<ResourceAccessDefinition, 1> resource_accesses;
	resource_accesses[0] = CreateTextureResourceAccess(resource_id, PipelineStagesMask::DepthStencilRW, ResourceAccessMask::DepthStencilRW);
	
	AppendResourceAccesses(record_context, resource_accesses);
}

void CmdSetRenderTargets(RecordContext* record_context, ArrayView<VirtualResourceID> resource_ids, VirtualResourceID depth_stencil) {
	auto& packet = AppendPacket<CmdSetRenderTargetsPacket>(record_context);
	packet.resource_ids = ArrayCopy(resource_ids, record_context->alloc);
	packet.depth_stencil_resource_id = depth_stencil;
	
	DebugAssert(resource_ids.count <= record_context->state_cache.render_targets.capacity, "Setting too many render targets. %llu/%llu", resource_ids.count, record_context->state_cache.render_targets.capacity);
	memcpy(record_context->state_cache.render_targets.data, resource_ids.data, resource_ids.count * sizeof(VirtualResourceID));
	record_context->state_cache.render_targets.count = resource_ids.count;
	record_context->state_cache.depth_stencil = depth_stencil;
}

void CmdSetRenderTargets(RecordContext* record_context, VirtualResourceID resource_id, VirtualResourceID depth_stencil) {
	CmdSetRenderTargets(record_context, { &resource_id, 1 }, depth_stencil);
}

void CmdSetViewportAndScissor(RecordContext* record_context, uint2 max, uint2 min) {
	CmdSetViewport(record_context, max, min);
	CmdSetScissor(record_context, max, min);
}

void CmdSetViewport(RecordContext* record_context, uint2 max, uint2 min) {
	auto& packet = AppendPacket<CmdSetViewportAndScissorPacket>(record_context);
	packet.packet_type = CommandType::SetViewport;
	packet.min = min;
	packet.max = max;
}

void CmdSetScissor(RecordContext* record_context, uint2 max, uint2 min) {
	auto& packet = AppendPacket<CmdSetViewportAndScissorPacket>(record_context);
	packet.packet_type = CommandType::SetScissor;
	packet.min = min;
	packet.max = max;
}

void CmdSetIndexBufferView(RecordContext* record_context, GpuAddress gpu_address, u32 size, TextureFormat format) {
	auto& packet = AppendPacket<CmdSetIndexBufferViewPacket>(record_context);
	packet.gpu_address = gpu_address;
	packet.size        = size;
	packet.format      = format;
}

void CmdSetRootSignature(RecordContext* record_context, const HLSL::BaseRootSignature& root_signature) {
	auto& packet = AppendPacket<CmdSetRootSignaturePacket>(record_context);
	packet.root_signature_id = root_signature.root_signature_id;
	packet.pass_type         = root_signature.pass_type;
	
	auto& state_cache = record_context->state_cache;
	state_cache.current_render_pass_type = root_signature.pass_type;
	state_cache.is_dirty = true;
	state_cache.resource_bindings.count = 0;
	ArrayResizeMemset(state_cache.resource_bindings, record_context->alloc, root_signature.root_parameter_count);
}

void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id) {
	auto& packet = AppendPacket<CmdSetPipelineStatePacket>(record_context);
	packet.pipeline_id = pipeline_id;
	
	auto& state_cache = record_context->state_cache;
	if (state_cache.depth_stencil_access != pipeline_id.depth_stencil_access) {
		state_cache.depth_stencil_access  = pipeline_id.depth_stencil_access;
		state_cache.is_dirty = true;
	}
	
	if (state_cache.stages_mask != pipeline_id.stages_mask) {
		state_cache.stages_mask  = pipeline_id.stages_mask;
		state_cache.is_dirty = true;
	}
}

void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table) {
	auto& packet = AppendPacket<CmdSetDescriptorTablePacket>(record_context);
	packet.offset    = offset;
	packet.pass_type = record_context->state_cache.current_render_pass_type;
	packet.descriptor_heap_offset = descriptor_table.descriptor_heap_offset;
	
	record_context->state_cache.resource_bindings[offset] = &descriptor_table;
	record_context->state_cache.is_dirty = true;
}

void CmdSetPushConstants(RecordContext* record_context, u32 offset, ArrayView<u32> push_constants) {
	auto& packet = AppendPacket<CmdSetPushConstantsPacket>(record_context);
	packet.offset    = offset;
	packet.pass_type = record_context->state_cache.current_render_pass_type;
	packet.push_constants = ArrayCopy(push_constants, record_context->alloc);
}

void CmdSetConstantBuffer(RecordContext* record_context, u32 offset, GpuAddress gpu_address) {
	auto& packet = AppendPacket<CmdSetConstantBufferPacket>(record_context);
	packet.offset    = offset;
	packet.pass_type = record_context->state_cache.current_render_pass_type;
	packet.gpu_address = gpu_address;
}

