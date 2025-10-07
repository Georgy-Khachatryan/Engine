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


void CmdClearRenderTarget(RecordContext* record_context, u64 rtv_heap_index) {
	auto& packet = AppendPacket<CmdClearRenderTargetPacket>(record_context);
	packet.rtv_heap_index = rtv_heap_index;
}

void CmdSetRenderTargets(RecordContext* record_context, ArrayView<u64> rtv_heap_indices) {
	auto& packet = AppendPacket<CmdSetRenderTargetsPacket>(record_context);
	packet.rtv_heap_indices = ArrayCopy(rtv_heap_indices, record_context->alloc);
}

void CmdSetViewportAndScissor(RecordContext* record_context, uint2 max, uint2 min) {
	auto& packet = AppendPacket<CmdSetViewportAndScissorPacket>(record_context);
	packet.min = min;
	packet.max = max;
}


void CmdSetRootSignature(RecordContext* record_context, const HLSL::BaseRootSignature& root_signature) {
	auto& packet = AppendPacket<CmdSetRootSignaturePacket>(record_context);
	packet.root_signature_index = root_signature.root_signature_index;
	
	record_context->resource_bindings.count = 0;
	record_context->resource_bindings_dirty = true;
	ArrayResizeMemset(record_context->resource_bindings, record_context->alloc, root_signature.root_parameter_count);
}

void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table) {
	auto& packet = AppendPacket<CmdSetDescriptorTablePacket>(record_context);
	packet.offset                 = offset;
	packet.descriptor_heap_offset = descriptor_table.descriptor_heap_offset;
	
	record_context->resource_bindings[offset] = &descriptor_table;
	record_context->resource_bindings_dirty = true;
}

void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id) {
	auto& packet = AppendPacket<CmdSetPipelineStatePacket>(record_context);
	packet.pipeline_id = pipeline_id;
}

