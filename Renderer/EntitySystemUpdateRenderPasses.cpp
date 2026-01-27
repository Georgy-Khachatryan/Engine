#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "EntitySystem/EntitySystem.h"

static void AllocateGpuComponentStreams(RecordContext* record_context, EntitySystemBase* entity_system) {
	extern ArrayView<EntityTypeInfo>    entity_type_info_table;
	extern ArrayView<ComponentTypeInfo> component_type_info_table;
	
	auto* resource_table = record_context->resource_table;
	for (auto& array : entity_system->entity_type_arrays) {
		auto& entity_type_info = entity_type_info_table[array.entity_type_id.index];
		auto virtual_resource_ids = entity_type_info_table[array.entity_type_id.index].virtual_resource_ids;
		
		for (u32 i = 0; i < entity_type_info.gpu_component_count; i += 1) {
			u32 component_stream_index = entity_type_info.cpu_component_count + i;
			
			auto component_type_id = entity_type_info.component_type_ids[component_stream_index];
			auto resource_id = (VirtualResourceID)entity_type_info.virtual_resource_ids[component_stream_index];
			auto type_info = component_type_info_table[component_type_id.index];
			
			auto& virtual_resource = resource_table->virtual_resources[(u32)resource_id];
			u32 capacity = type_info.component_type == ComponentType::GpuMask ? DivideAndRoundUp(array.capacity, 64u) : array.capacity;
			
			u32 new_size = (u32)(capacity * type_info.size_bytes);
			u32 old_size = virtual_resource.buffer.size;
			if (new_size <= old_size) continue;
			
			auto new_resource = CreateBufferResource(record_context->context, new_size);
			auto old_resource = virtual_resource.buffer.resource;
			
			resource_table->Set(resource_id, new_resource, new_size);
			
			if (old_resource.handle == nullptr) continue;
			
			auto old_resource_id = resource_table->AddTransient(old_resource, old_size);
			CmdCopyBufferToBuffer(record_context, old_resource_id, resource_id, old_size);
			
			ReleaseBufferResource(record_context->context, old_resource, ResourceReleaseCondition::EndOfThisGpuFrame);
		}
	}
}

void EntitySystemUpdateRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, EntitySystemUpdateShadersID);
}

void EntitySystemUpdateRenderPass::RecordPass(RecordContext* record_context) {
	AllocateGpuComponentStreams(record_context, world_system);
	AllocateGpuComponentStreams(record_context, asset_system);
	
	if (upload_buffers.count != 0) {
		CmdSetRootSignature(record_context, root_signature);
		CmdSetPipelineState(record_context, pipeline_id);
	}
	
	for (auto& upload_buffer : upload_buffers) {
		auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
		descriptor_table.src_data.Bind(upload_buffer.data_gpu_address, upload_buffer.count * upload_buffer.stride);
		descriptor_table.dst_indices.Bind(upload_buffer.indices_gpu_address, upload_buffer.count * sizeof(u32));
		descriptor_table.dst_data.Bind(upload_buffer.dst_data_gpu_address);
		descriptor_table.dst_prev_data.Bind(upload_buffer.dst_prev_data_gpu_address);
		
		RootSignature::PushConstants constants;
		constants.count  = upload_buffer.count;
		constants.stride = upload_buffer.stride;
		
		CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
		CmdSetRootArgument(record_context, root_signature.constants, constants);
		
		CmdDispatch(record_context, DivideAndRoundUp(upload_buffer.count, 128u));
	}
}
