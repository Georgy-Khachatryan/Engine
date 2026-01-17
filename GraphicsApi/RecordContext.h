#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMath.h"
#include "GraphicsApiTypes.h"
#include "GraphicsApi.h"

struct GraphicsContext;
struct VirtualResourceTable;

namespace HLSL {
	struct BaseDescriptorTable;
	struct BaseRootSignature;
	template<typename T> struct DescriptorTable;
	template<typename T> struct PushConstantBuffer;
	template<typename T> struct ConstantBuffer;
}

struct RecordContextStateCache {
	Array<HLSL::BaseDescriptorTable*> resource_bindings;
	
	FixedCapacityArray<VirtualResourceID, 8> render_targets;
	VirtualResourceID depth_stencil = (VirtualResourceID)0;
	
	CommandQueueType current_render_pass_type = CommandQueueType::Graphics;
	PipelineStagesMask stages_mask = PipelineStagesMask::None;
	DepthStencilAccess depth_stencil_access = DepthStencilAccess::None;
	
	bool is_dirty = false;
};

struct RecordContext {
	GraphicsContext* context = nullptr;
	
	StackAllocator* alloc = nullptr;
	u8* command_memory = nullptr;
	u32 remaining_size = 0; 
	u32 command_count  = 0;
	u8* command_memory_base = nullptr;
	
	u32 upload_buffer_offset = 0;
	u32 padding_0 = 0;
	
	Array<HLSL::BaseDescriptorTable*> descriptor_tables;
	VirtualResourceTable* resource_table = nullptr;
	
	Array<ArrayView<ResourceAccessDefinition>> resource_accesses;
	Array<u32> resource_access_command_prefix_sum;
	
	RecordContextStateCache state_cache;
};

void CmdDispatch(RecordContext* record_context, u32 group_count_x = 1, u32 group_count_y = 1, u32 group_count_z = 1);
void CmdDispatch(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z = 1);
void CmdDispatch(RecordContext* record_context, const uint3& group_count_xyz);
void CmdDispatchMesh(RecordContext* record_context, u32 group_count_x = 1, u32 group_count_y = 1, u32 group_count_z = 1);
void CmdDispatchMesh(RecordContext* record_context, uint2 group_count_xy, u32 group_count_z = 1);
void CmdDispatchMesh(RecordContext* record_context, const uint3& group_count_xyz);
void CmdDrawInstanced(RecordContext* record_context, u32 vertex_count_per_instance, u32 instance_count = 1, u32 start_vertex_location = 0, u32 start_instance_location = 0);
void CmdDrawIndexedInstanced(RecordContext* record_context, u32 index_count_per_instance, u32 instance_count = 1, u32 start_index_location = 0, u32 base_vertex_location = 0, u32 start_instance_location = 0);
void CmdDispatchIndirect(RecordContext* record_context, GpuAddress indirect_arguments);
void CmdDispatchMeshIndirect(RecordContext* record_context, GpuAddress indirect_arguments);
void CmdDrawInstancedIndirect(RecordContext* record_context, GpuAddress indirect_arguments);
void CmdDrawIndexedInstancedIndirect(RecordContext* record_context, GpuAddress indirect_arguments);
void CmdCopyBufferToTexture(RecordContext* record_context, GpuAddress src_buffer_gpu_address, VirtualResourceID dst_texture_resource_id, u32 src_row_pitch, uint3 src_size, uint3 dst_offset = 0, u32 dst_subresource_index = 0);
void CmdCopyBufferToBuffer(RecordContext* record_context, GpuAddress src_gpu_address, GpuAddress dst_gpu_address, u32 size);
void CmdClearRenderTarget(RecordContext* record_context, VirtualResourceID resource_id);
void CmdClearDepthStencil(RecordContext* record_context, VirtualResourceID resource_id);
void CmdSetRenderTargets(RecordContext* record_context, ArrayView<VirtualResourceID> resource_ids, VirtualResourceID depth_stencil = (VirtualResourceID)0);
void CmdSetRenderTargets(RecordContext* record_context, VirtualResourceID resource_id, VirtualResourceID depth_stencil = (VirtualResourceID)0);
void CmdSetViewportAndScissor(RecordContext* record_context, uint2 max, uint2 min = 0);
void CmdSetViewport(RecordContext* record_context, uint2 max, uint2 min = 0);
void CmdSetScissor(RecordContext* record_context, uint2 max, uint2 min = 0);
void CmdSetIndexBufferView(RecordContext* record_context, GpuAddress gpu_address, u32 size, TextureFormat format = TextureFormat::R16_UINT);

void CmdSetRootSignature(RecordContext* record_context, const HLSL::BaseRootSignature& root_signature);
void CmdSetPipelineState(RecordContext* record_context, PipelineID pipeline_id);
void CmdSetDescriptorTable(RecordContext* record_context, u32 offset, HLSL::BaseDescriptorTable& descriptor_table);
void CmdSetPushConstants(RecordContext* record_context, u32 offset, ArrayView<u32> push_constants);
void CmdSetConstantBuffer(RecordContext* record_context, u32 offset, GpuAddress gpu_address);

void ReplayRecordContext(GraphicsContext* context, RecordContext* record_context);


struct XessDispatchContext {
	VirtualResourceID xess_handle_resource_id;
	VirtualResourceID result_resource_id;
	VirtualResourceID radiance_resource_id;
	VirtualResourceID depth_resource_id;
	VirtualResourceID motion_vector_resource_id;
	float2 jitter_offset_pixels;
};
void CmdDispatchXeSS(RecordContext* record_context, const XessDispatchContext& dispatch_context);

struct DlssDispatchContext {
	VirtualResourceID dlss_handle_resource_id;
	VirtualResourceID result_resource_id;
	VirtualResourceID radiance_resource_id;
	VirtualResourceID depth_resource_id;
	VirtualResourceID motion_vector_resource_id;
	float2 jitter_offset_pixels;
};
void CmdDispatchDLSS(RecordContext* record_context, const DlssDispatchContext& dispatch_context);


template<typename T>
void CmdSetRootArgument(RecordContext* record_context, const HLSL::DescriptorTable<T>& root_descriptor_table, T& descriptor_table) {
	CmdSetDescriptorTable(record_context, root_descriptor_table.offset, descriptor_table);
}

template<typename T>
void CmdSetRootArgument(RecordContext* record_context, const HLSL::PushConstantBuffer<T>& push_constant_buffer, const T& push_constants) {
	CmdSetPushConstants(record_context, push_constant_buffer.offset, { (u32*)&push_constants, sizeof(T) / sizeof(u32) });
}

template<typename T>
void CmdSetRootArgument(RecordContext* record_context, const HLSL::ConstantBuffer<T>& constant_buffer, GpuAddress gpu_address) {
	CmdSetConstantBuffer(record_context, constant_buffer.offset, gpu_address);
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
inline u32 CreateResourceDescriptor(RecordContext* record_context, const T& descriptor, u32 descriptor_heap_index = u32_max) {
	struct DescriptorTable : HLSL::BaseDescriptorTable { T descriptor; };
	
	if (descriptor_heap_index == u32_max) {
		descriptor_heap_index = AllocateTransientSrvDescriptorTable(record_context->context, 1);
	}
	
	auto* descriptor_table = NewFromAlloc(record_context->alloc, DescriptorTable);
	descriptor_table->descriptor_heap_offset = descriptor_heap_index;
	descriptor_table->descriptor_count       = 1;
	descriptor_table->descriptor             = descriptor;
	ArrayAppend(record_context->descriptor_tables, record_context->alloc, descriptor_table);
	
	return descriptor_heap_index;
}

TextureSize GetTextureSize(RecordContext* record_context, VirtualResourceID resource_id);
VirtualResource& GetVirtualResource(RecordContext* record_context, VirtualResourceID resource_id);

template<typename T>
struct TransientBufferAllocation {
	GpuAddress gpu_address = {};
	T* cpu_address = nullptr;
};

template<typename T = u8, u32 alignment = 256u>
inline TransientBufferAllocation<T> AllocateTransientUploadBuffer(RecordContext* record_context, u32 size = 1u) {
	auto& upload_buffer = GetVirtualResource(record_context, VirtualResourceID::TransientUploadBuffer);
	
	u32 offset     = record_context->upload_buffer_offset;
	u32 size_bytes = (u32)(size * sizeof(T));
	
	if constexpr (alignment & (alignment - 1)) {
		offset     = RoundUp(offset,     alignment);
		size_bytes = RoundUp(size_bytes, alignment);
	} else {
		offset     = AlignUp(offset,     alignment);
		size_bytes = AlignUp(size_bytes, alignment);
	}
	
	TransientBufferAllocation<T> result;
	result.gpu_address = GpuAddress(VirtualResourceID::TransientUploadBuffer, offset);
	result.cpu_address = (T*)(upload_buffer.buffer.cpu_address + offset);
	
	offset += size_bytes;
	DebugAssert(offset <= upload_buffer.buffer.size, "Upload buffer overflow. (%/%).", offset, upload_buffer.buffer.size);
	record_context->upload_buffer_offset = offset;
	
	return result;
}

