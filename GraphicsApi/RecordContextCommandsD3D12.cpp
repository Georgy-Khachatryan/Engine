#include "GraphicsApiD3D12.h"
#include "RecordContext.h"
#include "RecordContextCommands.h"
#include "Basic/BasicMemory.h"


static void CmdClearRenderTargetD3D12(CmdClearRenderTargetPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
	command_list->ClearRenderTargetView({ packet->rtv_heap_index }, clear_color, 0, nullptr);
}

static void CmdSetRenderTargetsD3D12(CmdSetRenderTargetsPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->OMSetRenderTargets((u32)packet->rtv_heap_indices.count, (D3D12_CPU_DESCRIPTOR_HANDLE*)packet->rtv_heap_indices.data, false, nullptr);
}

static void CmdSetViewportAndScissorD3D12(CmdSetViewportAndScissorPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = (float)packet->min_x;
	viewport.TopLeftY = (float)packet->min_y;
	viewport.Width    = (float)(packet->max_x - packet->min_x);
	viewport.Height   = (float)(packet->max_y - packet->min_y);
	viewport.MinDepth = 0.f;
	viewport.MaxDepth = 1.f;
	command_list->RSSetViewports(1, &viewport);
	
	D3D12_RECT scissor = {};
	scissor.left   = (s32)packet->min_x;
	scissor.top    = (s32)packet->min_y;
	scissor.right  = (s32)packet->max_x;
	scissor.bottom = (s32)packet->max_y;
	command_list->RSSetScissorRects(1, &scissor);
}

static void CmdDispatchD3D12(CmdDispatchPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->Dispatch(packet->group_count_x, packet->group_count_y, packet->group_count_z);
}

static void CmdDrawInstancedD3D12(CmdDrawInstancedPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DrawInstanced(packet->vertex_count_per_instance, packet->instance_count, packet->start_vertex_location, packet->start_instance_location);
}

static void CmdDrawIndexedInstancedD3D12(CmdDrawIndexedInstancedPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DrawIndexedInstanced(packet->index_count_per_instance, packet->instance_count, packet->start_index_location, packet->base_vertex_location, packet->start_instance_location);
}


void ReplayRecordContext(GraphicsContext* api_context, RecordContext* record_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	
	auto* command_list = context->command_list;
	
	u32 command_count  = record_context->command_count;
	u8* command_memory = record_context->command_memory_base;
	for (u32 i = 0; i < command_count; i += 1) {
		auto* packet = (RecordContextCommandPacket*)command_memory;
		command_memory += packet->packet_size;
		
		switch (packet->packet_type) {
		case CommandType::Jump: command_memory = ((CmdJumpPacket*)packet)->command_memory; break;
		case CommandType::Dispatch:              CmdDispatchD3D12((CmdDispatchPacket*)packet, command_list); break;
		case CommandType::DrawInstanced:         CmdDrawInstancedD3D12((CmdDrawInstancedPacket*)packet, command_list); break;
		case CommandType::DrawIndexedInstanced:  CmdDrawIndexedInstancedD3D12((CmdDrawIndexedInstancedPacket*)packet, command_list); break;
		case CommandType::ClearRenderTarget:     CmdClearRenderTargetD3D12((CmdClearRenderTargetPacket*)packet, command_list); break;
		case CommandType::SetRenderTargets:      CmdSetRenderTargetsD3D12((CmdSetRenderTargetsPacket*)packet, command_list); break;
		case CommandType::SetViewportAndScissor: CmdSetViewportAndScissorD3D12((CmdSetViewportAndScissorPacket*)packet, command_list); break;
		default: DebugAssertAlways("Unhandled command packet type '%u'.", (u32)packet->packet_type); i = command_count; break;
		}
	}
}

