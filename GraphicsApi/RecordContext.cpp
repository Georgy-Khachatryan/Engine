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

void CmdClearRenderTarget(RecordContext* record_context, u64 rtv_heap_index) {
	auto& packet = AppendPacket<CmdClearRenderTargetPacket>(record_context);
	packet.rtv_heap_index = rtv_heap_index;
}
