#pragma once
#include "Basic/Basic.h"


enum struct CommandType : u16 {
	None                  = 0,
	Jump                  = 1,
	Dispatch              = 2,
	DrawInstanced         = 3,
	DrawIndexedInstanced  = 4,
	ClearRenderTarget     = 5,
	SetRenderTargets      = 6,
	SetViewportAndScissor = 7,
	SetRootSignature      = 8,
	SetDescriptorTable    = 9,
	SetPipelineState      = 10,
	
	Count
};

struct RecordContextCommandPacket {
	CommandType packet_type = CommandType::None;
	u16 packet_size = 0;
};

struct CmdJumpPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::Jump;
	
	u8* command_memory = nullptr;
};

struct CmdDispatchPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::Dispatch;
	
	uint3 group_count;
};

struct CmdDrawInstancedPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::DrawInstanced;
	
	u32 vertex_count_per_instance = 0;
	u32 instance_count            = 0;
	u32 start_vertex_location     = 0;
	u32 start_instance_location   = 0;
};

struct CmdDrawIndexedInstancedPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::DrawIndexedInstanced;
	
	u32 index_count_per_instance = 0;
	u32 instance_count           = 0;
	u32 start_index_location     = 0;
	u32 base_vertex_location     = 0;
	u32 start_instance_location  = 0;
};

struct CmdClearRenderTargetPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::ClearRenderTarget;
	
	u64 rtv_heap_index = 0;
};

struct CmdSetRenderTargetsPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetRenderTargets;
	
	ArrayView<u64> rtv_heap_indices;
};

struct CmdSetViewportAndScissorPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetViewportAndScissor;
	
	uint2 min = 0;
	uint2 max = 0;
};

struct CmdSetRootSignaturePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetRootSignature;
	
	u32 root_signature_index = 0;
};

struct CmdSetDescriptorTablePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetDescriptorTable;
	
	u32 offset = 0;
	u32 descriptor_heap_offset = 0;
};

struct CmdSetPipelineStatePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetPipelineState;
	
	PipelineID pipeline_id;
};
