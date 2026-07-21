#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "EntitySystem/EntitySystem.h"

static void AllocateGpuComponentStreams(RecordContext* record_context, EntitySystemBase* entity_system) {
	extern ArrayView<EntityTypeInfo>    entity_type_info_table;
	extern ArrayView<ComponentTypeInfo> component_type_info_table;
	
	bool clear_gpu_mask_component_streams = entity_system->clear_gpu_mask_component_streams;
	entity_system->clear_gpu_mask_component_streams = false;
	
	auto* resource_table = record_context->resource_table;
	for (auto& allocation : entity_system->gpu_component_stream_allocations) {
		auto& array = entity_system->entity_type_arrays[allocation.entity_type_id.index];
		if (array.capacity == 0) continue;
		
		auto type_info = component_type_info_table[allocation.component_type_id.index];
		
		// TODO: Fill buffer with zeroes instead of recreating it.
		bool clear_component_stream = clear_gpu_mask_component_streams && (type_info.component_type == ComponentType::GpuMask);
		
		u32 capacity = type_info.component_type == ComponentType::GpuMask ? DivideAndRoundUp(array.capacity, 64u) : array.capacity;
		u32 new_size = (u32)(capacity * type_info.size_bytes);
		u32 old_size = allocation.size;
		
		if (old_size < new_size || clear_component_stream) {
			auto new_resource = CreateBufferResource(record_context->context, new_size, CreateResourceFlags::UAV);
			auto old_resource = NativeBufferResource{ allocation.handle };
			
			ReleaseBufferResource(record_context->context, old_resource, ResourceReleaseCondition::EndOfThisGpuFrame);
			
			allocation.handle = new_resource.handle;
			allocation.size   = new_size;
			
			if (old_resource.handle != nullptr && clear_component_stream == false) {
				auto old_resource_id = resource_table->AddTransient(old_resource, old_size);
				CmdCopyBufferToBuffer(record_context, old_resource_id, allocation.resource_id, old_size);
			}
		}
		
		resource_table->Set(allocation.resource_id, { allocation.handle }, allocation.size);
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
