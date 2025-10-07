#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMath.h"
#include "GraphicsApiTypes.h"

struct GraphicsContext;
struct VirtualResourceTable;

namespace HLSL {
	struct BaseDescriptorTable;
	struct BaseRootSignature;
	template<typename T> struct DescriptorTable;
}

struct RecordContext {
	GraphicsContext* context = nullptr;
	
	StackAllocator* alloc = nullptr;
	u8* command_memory = nullptr;
	u32 remaining_size = 0; 
	u32 command_count  = 0;
	u8* command_memory_base = nullptr;
	
	Array<HLSL::BaseDescriptorTable*> descriptor_tables;
	VirtualResourceTable* resource_table = nullptr;
	
	Array<ArrayView<ResourceAccessDefinition>> resource_accesses;
	Array<u32> resource_access_command_prefix_sum;
	
	Array<HLSL::BaseDescriptorTable*> resource_bindings;
	bool resource_bindings_dirty = false;
};

void CmdDispatch(RecordContext* record_context, u32 group_count_x = 1, u32 group_count_y = 1, u32 group_count_z = 1);
void CmdDispatch(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z = 1);
void CmdDispatch(RecordContext* record_context, const uint3& group_count_xyz);
void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count = 1, u32 start_vertex_location = 0, u32 start_instance_location = 0);
void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count = 1, u32 start_index_location = 0, u32 base_vertex_location = 0, u32 start_instance_location = 0);
void CmdClearRenderTarget(RecordContext* record_context, u64 rtv_heap_index);
void CmdSetRenderTargets(RecordContext* record_context, ArrayView<u64> rtv_heap_indices);
void CmdSetViewportAndScissor(RecordContext* record_context, uint2 max, uint2 min = 0);

void CmdSetRootSignature(RecordContext* record_context, const HLSL::BaseRootSignature& root_signature);
void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table);
void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id);

void ReplayRecordContext(GraphicsContext* context, RecordContext* record_context);


template<typename T>
static T& AllocateDescriptorTable(RecordContext* record_context, const HLSL::DescriptorTable<T>& root_descriptor_table) {
	auto* descriptor_table = NewFromAlloc(record_context->alloc, T);
	descriptor_table->descriptor_heap_offset = AllocateTransientSrvDescriptorTable(record_context->context, root_descriptor_table.descriptor_count);
	descriptor_table->descriptor_count       = root_descriptor_table.descriptor_count;
	
	ArrayAppend(record_context->descriptor_tables, record_context->alloc, descriptor_table);
	
	return *descriptor_table;
}

template<typename T>
void CmdSetRootArgument(RecordContext* record_context, const HLSL::DescriptorTable<T>& root_descriptor_table, HLSL::BaseDescriptorTable& descriptor_table) {
	CmdSetDescriptorTable(record_context, root_descriptor_table.offset, descriptor_table);
}
