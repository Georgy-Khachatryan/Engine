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


void CmdDispatch(RecordContext* record_context, u32 group_count_x, u32 group_count_y, u32 group_count_z) {
	auto& packet = AppendPacket<CmdDispatchPacket>(record_context);
	packet.group_count_x = group_count_x;
	packet.group_count_y = group_count_y;
	packet.group_count_z = group_count_z;
}

void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location) {
	auto& packet = AppendPacket<CmdDrawInstancedPacket>(record_context);
	packet.vertex_count_per_instance = vertex_count_per_instance;
	packet.instance_count            = instance_count;
	packet.start_vertex_location     = start_vertex_location;
	packet.start_instance_location   = start_instance_location;
}

void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count, u32 start_index_location, u32 base_vertex_location, u32 start_instance_location) {
	auto& packet = AppendPacket<CmdDrawIndexedInstancedPacket>(record_context);
	packet.index_count_per_instance = index_count_per_instance;
	packet.instance_count           = instance_count;
	packet.start_index_location     = start_index_location;
	packet.base_vertex_location     = base_vertex_location;
	packet.start_instance_location  = start_instance_location;
}


void CmdClearRenderTarget(RecordContext* record_context, u64 rtv_heap_index) {
	auto& packet = AppendPacket<CmdClearRenderTargetPacket>(record_context);
	packet.rtv_heap_index = rtv_heap_index;
}

void CmdSetRenderTargets(RecordContext* record_context, ArrayView<u64> rtv_heap_indices) {
	auto& packet = AppendPacket<CmdSetRenderTargetsPacket>(record_context);
	packet.rtv_heap_indices = ArrayCopy(rtv_heap_indices, record_context->alloc);
}

void CmdSetViewportAndScissor(RecordContext* record_context, u32 max_x, u32 max_y, u32 min_x, u32 min_y) {
	auto& packet = AppendPacket<CmdSetViewportAndScissorPacket>(record_context);
	packet.min_x = min_x;
	packet.min_y = min_y;
	packet.max_x = max_x;
	packet.max_y = max_y;
}


void CmdSetRootSignature(RecordContext* record_context, const HLSL::BaseRootSignature& root_signature) {
	auto& packet = AppendPacket<CmdSetRootSignaturePacket>(record_context);
	packet.root_signature_index = root_signature.root_signature_index;
}

void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table) {
	auto& packet = AppendPacket<CmdSetDescriptorTablePacket>(record_context);
	packet.offset                 = offset;
	packet.descriptor_heap_offset = descriptor_table.descriptor_heap_offset;
}

void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id) {
	auto& packet = AppendPacket<CmdSetPipelineStatePacket>(record_context);
	packet.pipeline_id = pipeline_id;
}

