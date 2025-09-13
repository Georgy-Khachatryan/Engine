#pragma once
#include "Basic/Basic.h"


enum struct CommandType : u16 {
	None              = 0,
	Jump              = 1,
	ClearRenderTarget = 2,
	
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

struct CmdClearRenderTargetPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::ClearRenderTarget;
	u64 rtv_heap_index = 0;
};

