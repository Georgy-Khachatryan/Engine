#include "RecordContext.h"

#include "Basic/BasicMemory.h"
#include "RecordContextCommands.h"
#include "Engine/RenderPasses.h"

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
	record_context->resource_bindings_dirty = true;

	ArrayAppend(record_context->resource_accesses, record_context->alloc, ArrayCopy(resource_accesses, record_context->alloc));
	ArrayAppend(record_context->resource_access_command_prefix_sum, record_context->alloc, record_context->command_count);
}

static void AppendResourceBindings(RecordContext* record_context, PipelineStagesMask stages_mask) {
	if (record_context->resource_bindings_dirty == false) {
		ArrayLastElement(record_context->resource_access_command_prefix_sum) = record_context->command_count;
		return;
	}
	record_context->resource_bindings_dirty = false;
	
	u32 accessed_resource_count = 0;
	for (auto* descriptor_table : record_context->resource_bindings) {
		if (descriptor_table == nullptr) continue;
		accessed_resource_count += descriptor_table->descriptor_count;
	}
	
	if (HasAnyFlags(stages_mask, PipelineStagesMask::PixelShader)) {
		accessed_resource_count += (u32)record_context->render_targets.count;
	}
	
	
	Array<ResourceAccessDefinition> resource_accesses;
	ArrayReserve(resource_accesses, record_context->alloc, accessed_resource_count);
	
	for (auto* descriptor_table : record_context->resource_bindings) {
		if (descriptor_table == nullptr) continue;
		
		auto descriptors = ArrayView<HLSL::ResourceDescriptor>{ (HLSL::ResourceDescriptor*)(descriptor_table + 1), (u64)descriptor_table->descriptor_count };
		for (auto& descriptor : descriptors) {
			u32 type = (u32)descriptor.common.type;
			if (type & (u32)HLSL::ResourceDescriptorType::AnyTexture) {
				ResourceAccessDefinition access;
				access.resource_id = descriptor.resource_id;
				access.array_index = descriptor.texture.array_index;
				access.array_count = descriptor.texture.array_count;
				access.mip_index   = descriptor.texture.mip_index;
				access.mip_count   = descriptor.texture.mip_count;
				access.is_texture  = true;
				access.stages_mask = stages_mask;
				access.access_mask = type & (u32)HLSL::ResourceDescriptorType::AnySRV ? ResourceAccessMask::SRV : ResourceAccessMask::UAV;
				
				ArrayAppend(resource_accesses, access);
			} else if (type & (u32)HLSL::ResourceDescriptorType::AnyBuffer) {
				ResourceAccessDefinition access;
				access.resource_id = descriptor.resource_id;
				access.is_texture  = false;
				access.stages_mask = stages_mask;
				access.access_mask = type & (u32)HLSL::ResourceDescriptorType::AnySRV ? ResourceAccessMask::SRV : ResourceAccessMask::UAV;
				
				ArrayAppend(resource_accesses, access);
			}
		}
	}
	
	if (HasAnyFlags(stages_mask, PipelineStagesMask::PixelShader)) {
		for (auto render_target : record_context->render_targets) {
			ResourceAccessDefinition access;
			access.resource_id = render_target;
			access.array_index = 0;
			access.array_count = 1;
			access.mip_index   = 0;
			access.mip_count   = 1;
			access.is_texture  = true;
			access.stages_mask = PipelineStagesMask::RenderTarget;
			access.access_mask = ResourceAccessMask::RenderTarget;
			
			ArrayAppend(resource_accesses, access);
		}
	}
	
	
	ArrayAppend(record_context->resource_accesses, record_context->alloc, resource_accesses);
	ArrayAppend(record_context->resource_access_command_prefix_sum, record_context->alloc, record_context->command_count);
}

void CmdDispatch(RecordContext* record_context, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	auto& packet = AppendPacket<CmdDispatchPacket>(record_context);
	packet.group_count = uint3(group_count_x, group_count_y, group_count_z);
	AppendResourceBindings(record_context, PipelineStagesMask::ComputeShader);
}

void CmdDispatch(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z) {
	CmdDispatch(record_context, group_count_xy.x, group_count_xy.y, group_count_z);
}

void CmdDispatch(RecordContext* record_context, const uint3& group_count_xyz) {
	CmdDispatch(record_context, group_count_xyz.x, group_count_xyz.y, group_count_xyz.z);
}

void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location) {
	auto& packet = AppendPacket<CmdDrawInstancedPacket>(record_context);
	packet.vertex_count_per_instance = vertex_count_per_instance;
	packet.instance_count            = instance_count;
	packet.start_vertex_location     = start_vertex_location;
	packet.start_instance_location   = start_instance_location;
	AppendResourceBindings(record_context, PipelineStagesMask::VertexShader | PipelineStagesMask::PixelShader);
}

void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count, u32 start_index_location, u32 base_vertex_location, u32 start_instance_location) {
	auto& packet = AppendPacket<CmdDrawIndexedInstancedPacket>(record_context);
	packet.index_count_per_instance = index_count_per_instance;
	packet.instance_count           = instance_count;
	packet.start_index_location     = start_index_location;
	packet.base_vertex_location     = base_vertex_location;
	packet.start_instance_location  = start_instance_location;
	AppendResourceBindings(record_context, PipelineStagesMask::VertexShader | PipelineStagesMask::PixelShader);
}

void CmdCopyBufferToTexture(RecordContext* record_context, GpuAddress src_buffer_gpu_address, VirtualResourceID dst_texture_resource_id, u32 src_row_pitch, uint3 src_size, uint3 dst_offset, u32 dst_subresource_index) {
	auto& packet = AppendPacket<CmdCopyBufferToTexturePacket>(record_context);
	packet.src_buffer_gpu_address  = src_buffer_gpu_address;
	packet.dst_texture_resource_id = dst_texture_resource_id;
	packet.src_row_pitch           = src_row_pitch;
	packet.src_size                = src_size;
	packet.dst_subresource_index   = dst_subresource_index;
	packet.dst_offset              = dst_offset;
	
	FixedCountArray<ResourceAccessDefinition, 2> resource_accesses;
	resource_accesses[0].resource_id = src_buffer_gpu_address.resource_id;
	resource_accesses[0].is_texture  = false;
	resource_accesses[0].stages_mask = PipelineStagesMask::Copy;
	resource_accesses[0].access_mask = ResourceAccessMask::CopySrc;
	
	resource_accesses[1].resource_id = dst_texture_resource_id;
	resource_accesses[1].is_texture  = true;
	resource_accesses[1].stages_mask = PipelineStagesMask::Copy;
	resource_accesses[1].access_mask = ResourceAccessMask::CopyDst;
	
	DebugAssert(dst_subresource_index == 0, "TODO: Add support for subresources.");
	resource_accesses[1].mip_index   = 0;
	resource_accesses[1].mip_count   = 0;
	resource_accesses[1].array_index = 0;
	resource_accesses[1].array_count = 0;
	
	AppendResourceAccesses(record_context, resource_accesses);
}


void CmdClearRenderTarget(RecordContext* record_context, VirtualResourceID resource_id) {
	auto& packet = AppendPacket<CmdClearRenderTargetPacket>(record_context);
	packet.resource_id = resource_id;
	
	FixedCountArray<ResourceAccessDefinition, 1> resource_accesses;
	resource_accesses[0].resource_id = resource_id;
	resource_accesses[0].is_texture  = true;
	resource_accesses[0].stages_mask = PipelineStagesMask::RenderTarget;
	resource_accesses[0].access_mask = ResourceAccessMask::RenderTarget;
	resource_accesses[0].mip_index   = 0;
	resource_accesses[0].mip_count   = 0;
	resource_accesses[0].array_index = 0;
	resource_accesses[0].array_count = 0;
	
	AppendResourceAccesses(record_context, resource_accesses);
}

void CmdSetRenderTargets(RecordContext* record_context, ArrayView<VirtualResourceID> resource_ids) {
	auto& packet = AppendPacket<CmdSetRenderTargetsPacket>(record_context);
	packet.resource_ids = ArrayCopy(resource_ids, record_context->alloc);
	
	DebugAssert(resource_ids.count <= record_context->render_targets.capacity, "Setting too many render targets. %llu/%llu", resource_ids.count, record_context->render_targets.capacity);
	memcpy(record_context->render_targets.data, resource_ids.data, resource_ids.count * sizeof(VirtualResourceID));
	record_context->render_targets.count = resource_ids.count;
}

void CmdSetRenderTargets(RecordContext* record_context, VirtualResourceID resource_id) {
	CmdSetRenderTargets(record_context, { &resource_id, 1 });
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
	packet.root_signature_index = root_signature.root_signature_index;
	packet.pass_type            = root_signature.pass_type;
	
	record_context->current_render_pass_type = root_signature.pass_type;
	record_context->resource_bindings.count = 0;
	record_context->resource_bindings_dirty = true;
	ArrayResizeMemset(record_context->resource_bindings, record_context->alloc, root_signature.root_parameter_count);
}

void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id) {
	auto& packet = AppendPacket<CmdSetPipelineStatePacket>(record_context);
	packet.pipeline_id = pipeline_id;
}

void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table) {
	auto& packet = AppendPacket<CmdSetDescriptorTablePacket>(record_context);
	packet.offset    = offset;
	packet.pass_type = record_context->current_render_pass_type;
	packet.descriptor_heap_offset = descriptor_table.descriptor_heap_offset;
	
	record_context->resource_bindings[offset] = &descriptor_table;
	record_context->resource_bindings_dirty = true;
}

void CmdSetPushConstants(RecordContext* record_context, u32 offset, ArrayView<u32> push_constants) {
	auto& packet = AppendPacket<CmdSetPushConstantsPacket>(record_context);
	packet.offset    = offset;
	packet.pass_type = record_context->current_render_pass_type;
	packet.push_constants = ArrayCopy(push_constants, record_context->alloc);
}

void CmdSetConstantBuffer(RecordContext* record_context, u32 offset, GpuAddress gpu_address) {
	auto& packet = AppendPacket<CmdSetConstantBufferPacket>(record_context);
	packet.offset    = offset;
	packet.pass_type = record_context->current_render_pass_type;
	packet.gpu_address = gpu_address;
}

