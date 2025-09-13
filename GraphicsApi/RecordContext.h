#pragma once
#include "Basic/Basic.h"

struct GraphicsContext;

struct RecordContext {
	StackAllocator* alloc = nullptr;
	u8* command_memory = nullptr;
	u32 remaining_size = 0; 
	u32 command_count  = 0;
	u8* command_memory_base = nullptr;
};

void CmdClearRenderTarget(RecordContext* command_buffer, u64 rtv_heap_index);

void ReplayRecordContext(GraphicsContext* context, RecordContext* command_buffer);

