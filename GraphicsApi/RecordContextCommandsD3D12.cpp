#include "GraphicsApiD3D12.h"
#include "RecordContext.h"
#include "RecordContextCommands.h"
#include "Basic/BasicMemory.h"


static void CmdClearRenderTargetD3D12(CmdClearRenderTargetPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
	command_list->ClearRenderTargetView({ packet->rtv_heap_index }, clear_color, 0, nullptr);
}

void ReplayRecordContext(GraphicsContext* api_context, RecordContext* command_buffer) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto* command_list = context->command_list;
	
	u32 command_count  = command_buffer->command_count;
	u8* command_memory = command_buffer->command_memory_base;
	for (u32 i = 0; i < command_count; i += 1) {
		auto* packet = (RecordContextCommandPacket*)command_memory;
		command_memory += packet->packet_size;
		
		switch (packet->packet_type) {
		case CommandType::Jump: command_memory = ((CmdJumpPacket*)packet)->command_memory; break;
		case CommandType::ClearRenderTarget: CmdClearRenderTargetD3D12((CmdClearRenderTargetPacket*)packet, command_list); break;
		default: DebugAssertAlways("Unhandled command packet type '%u'.", (u32)packet->packet_type); i = command_count; break;
		}
	}
}

