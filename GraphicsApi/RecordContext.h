#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"

struct GraphicsContext;

struct RecordContext {
	StackAllocator* alloc = nullptr;
	u8* command_memory = nullptr;
	u32 remaining_size = 0; 
	u32 command_count  = 0;
	u8* command_memory_base = nullptr;
};

void CmdDispatch(RecordContext* record_context, u32 group_count_x = 1, u32 group_count_y = 1, u32 group_count_z = 1);
void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count = 1, u32 start_vertex_location = 0, u32 start_instance_location = 0);
void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count = 1, u32 start_index_location = 0, u32 base_vertex_location = 0, u32 start_instance_location = 0);
void CmdClearRenderTarget(RecordContext* record_context, u64 rtv_heap_index);
void CmdSetRenderTargets(RecordContext* record_context, ArrayView<u64> rtv_heap_indices);
void CmdSetViewportAndScissor(RecordContext* record_context, u32 max_x, u32 max_y, u32 min_x = 0, u32 min_y = 0);

void ReplayRecordContext(GraphicsContext* context, RecordContext* record_context);

