#include "Renderer.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/RecordContext.h"
#include "RenderPasses.h"

compile_const u32 upload_buffer_size     = 8 * 1024 * 1024;
compile_const u32 mesh_asset_buffer_size = 32 * 1024 * 1024;
compile_const u32 max_transient_resource_table_entries = 16;

RendererContext* CreateRendererContext(StackAllocator* alloc) {
	auto* context = NewFromAlloc(alloc, RendererContext);
	
	auto* graphics_context = CreateGraphicsContext(alloc);
	
	for (u32 i = 0; i < number_of_frames_in_flight; i += 1) {
		context->upload_buffers[i] = CreateBufferResource(graphics_context, upload_buffer_size, GpuMemoryAccessType::Upload, &context->upload_buffer_cpu_addresses[i]);
	}
	
	context->graphics_context       = graphics_context;
	context->async_transfer_queue   = CreateAsyncTransferQueue(alloc, graphics_context);
	context->mesh_asset_buffer      = CreateBufferResource(graphics_context, mesh_asset_buffer_size);
	context->mesh_asset_buffer_size = mesh_asset_buffer_size;
	context->debug_geometry_buffer  = DebugGeometryRenderPass::CreateDebugGeometryBuffer(alloc, graphics_context, context->async_transfer_queue);
	
	return context;
}

void ReleaseRendererContext(RendererContext* context, StackAllocator* alloc) {
	ReleaseBufferResource(context->graphics_context, context->debug_geometry_buffer.resource);
	ReleaseBufferResource(context->graphics_context, context->mesh_asset_buffer);
	ReleaseAsyncTransferQueue(context->async_transfer_queue);
	
	for (auto& buffer : context->upload_buffers) {
		ReleaseBufferResource(context->graphics_context, buffer);
	}
	
	ReleaseGraphicsContext(context->graphics_context, alloc);
}

VirtualResourceTable* CreateResourceTable(StackAllocator* alloc) {
	auto* resource_table = NewFromAlloc(alloc, VirtualResourceTable);
	ArrayReserve(resource_table->virtual_resources, alloc, (u64)VirtualResourceID::Count + max_transient_resource_table_entries);
	ArrayResizeMemset(resource_table->virtual_resources, alloc, (u64)VirtualResourceID::Count);
	
	return resource_table;
}

void ReleaseResourceTable(GraphicsContext* graphics_context, VirtualResourceTable* resource_table) {
	for (auto& resource : resource_table->virtual_resources) {
		if (resource.type == VirtualResource::Type::VirtualBuffer) {
			ReleaseBufferResource(graphics_context, resource.buffer.resource);
		} else if (resource.type == VirtualResource::Type::VirtualTexture) {
			ReleaseTextureResource(graphics_context, resource.texture.resource);
		} else if (resource.type == VirtualResource::Type::Opaque) {
			resource.opaque.release_user_data(&resource, graphics_context);
		}
	}
	
	ReleaseBufferResource(graphics_context, resource_table->virtual_resources[(u32)VirtualResourceID::GpuMeshAssetData].buffer.resource);
	ReleaseBufferResource(graphics_context, resource_table->virtual_resources[(u32)VirtualResourceID::GpuMeshEntityData].buffer.resource);
	ReleaseBufferResource(graphics_context, resource_table->virtual_resources[(u32)VirtualResourceID::MeshEntityAliveMask].buffer.resource);
	ReleaseBufferResource(graphics_context, resource_table->virtual_resources[(u32)VirtualResourceID::MeshEntityGpuTransform].buffer.resource);
	ReleaseBufferResource(graphics_context, resource_table->virtual_resources[(u32)VirtualResourceID::SceneConstants].buffer.resource);
}

RecordContext* BeginRecordContext(StackAllocator* alloc, RendererContext* context, WindowSwapChain* swap_chain, VirtualResourceTable* resource_table) {
	auto* record_context = NewFromAlloc(alloc, RecordContext);
	record_context->alloc          = alloc;
	record_context->context        = context->graphics_context;
	record_context->resource_table = resource_table;
	
	resource_table->Set(VirtualResourceID::CurrentBackBuffer, WindowSwapGetCurrentBackBuffer(swap_chain), swap_chain->size);
	resource_table->Set(VirtualResourceID::MeshAssetBuffer, context->mesh_asset_buffer, mesh_asset_buffer_size);
	resource_table->Set(VirtualResourceID::DebugMeshBuffer, context->debug_geometry_buffer.resource, (u32)context->debug_geometry_buffer.resource_size);
	resource_table->Set(VirtualResourceID::TransientUploadBuffer, context->upload_buffers[context->upload_buffer_index], upload_buffer_size, context->upload_buffer_cpu_addresses[context->upload_buffer_index]);
	context->upload_buffer_index = (context->upload_buffer_index + 1) % number_of_frames_in_flight;
	
	return record_context;
}
