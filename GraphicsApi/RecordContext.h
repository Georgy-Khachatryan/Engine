#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMath.h"
#include "GraphicsApiTypes.h"

struct GraphicsContext;
struct VirtualResourceTable;
enum struct RenderPassType : u32;

namespace HLSL {
	struct BaseDescriptorTable;
	struct BaseRootSignature;
	template<typename T> struct DescriptorTable;
	template<typename T> struct PushConstantBuffer;
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
	
	RenderPassType current_render_pass_type;
	Array<HLSL::BaseDescriptorTable*> resource_bindings;
	FixedCapacityArray<VirtualResourceID, 8> render_targets;
	bool resource_bindings_dirty = false;
};

void CmdDispatch(RecordContext* record_context, u32 group_count_x = 1, u32 group_count_y = 1, u32 group_count_z = 1);
void CmdDispatch(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z = 1);
void CmdDispatch(RecordContext* record_context, const uint3& group_count_xyz);
void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count = 1, u32 start_vertex_location = 0, u32 start_instance_location = 0);
void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count = 1, u32 start_index_location = 0, u32 base_vertex_location = 0, u32 start_instance_location = 0);
void CmdCopyBufferToTexture(RecordContext* record_context, GpuAddress src_buffer_gpu_address, VirtualResourceID dst_texture_resource_id, u32 src_row_pitch, uint3 src_size, uint3 dst_offset = 0, u32 dst_subresource_index = 0);
void CmdClearRenderTarget(RecordContext* record_context, VirtualResourceID resource_id);
void CmdSetRenderTargets(RecordContext* record_context, ArrayView<VirtualResourceID> resource_ids);
void CmdSetRenderTargets(RecordContext* record_context, VirtualResourceID resource_id);
void CmdSetViewportAndScissor(RecordContext* record_context, uint2 max, uint2 min = 0);
void CmdSetViewport(RecordContext* record_context, uint2 max, uint2 min = 0);
void CmdSetScissor(RecordContext* record_context, uint2 max, uint2 min = 0);
void CmdSetIndexBufferView(RecordContext* record_context, GpuAddress gpu_address, u32 size, TextureFormat format = TextureFormat::R16_UINT);

void CmdSetRootSignature(RecordContext* record_context, const HLSL::BaseRootSignature& root_signature);
void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table);
void CmdSetPushConstants(RecordContext* record_context, u32 offset, ArrayView<u32> push_constants);
void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id);

void ReplayRecordContext(GraphicsContext* context, RecordContext* record_context);


template<typename T>
void CmdSetRootArgument(RecordContext* record_context, const HLSL::DescriptorTable<T>& root_descriptor_table, T& descriptor_table) {
	CmdSetDescriptorTable(record_context, root_descriptor_table.offset, descriptor_table);
}

template<typename T>
void CmdSetRootArgument(RecordContext* record_context, const HLSL::PushConstantBuffer<T>& push_constant_buffer, T& push_constants) {
	CmdSetPushConstants(record_context, push_constant_buffer.offset, { (u32*)&push_constants, sizeof(T) / sizeof(u32) });
}


template<typename T>
inline T& AllocateDescriptorTable(RecordContext* record_context, const HLSL::DescriptorTable<T>& root_descriptor_table) {
	auto* descriptor_table = NewFromAlloc(record_context->alloc, T);
	descriptor_table->descriptor_heap_offset = AllocateTransientSrvDescriptorTable(record_context->context, root_descriptor_table.descriptor_count);
	descriptor_table->descriptor_count       = root_descriptor_table.descriptor_count;
	
	ArrayAppend(record_context->descriptor_tables, record_context->alloc, descriptor_table);
	
	return *descriptor_table;
}

template<typename T>
inline void CreateResourceDescriptor(RecordContext* record_context, const T& descriptor, u32 descriptor_heap_index) {
	struct DescriptorTable : HLSL::BaseDescriptorTable { T descriptor; };
	
	auto* descriptor_table = NewFromAlloc(record_context->alloc, DescriptorTable);
	descriptor_table->descriptor_heap_offset = descriptor_heap_index;
	descriptor_table->descriptor_count       = 1;
	descriptor_table->descriptor             = descriptor;
	ArrayAppend(record_context->descriptor_tables, record_context->alloc, descriptor_table);
}

