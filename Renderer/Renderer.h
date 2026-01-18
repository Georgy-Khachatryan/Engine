#pragma once
#include "Basic/Basic.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct GraphicsContext;
struct WindowSwapChain;
struct AsyncTransferQueue;
struct VirtualResourceTable;
struct EntitySystem;

struct DebugMeshLayout {
	u32 vertex_offset = 0;
	u32 vertex_count  = 0;
	u32 index_offset  = 0;
	u32 index_count   = 0;
};

struct DebugGeometryBuffer {
	NativeBufferResource resource;
	u64 resource_size = 0;
	
	FixedCountArray<DebugMeshLayout, 4> mesh_layouts;
	
	u32 vertex_count = 0;
	u32 index_count  = 0;
};


struct RendererContext {
	GraphicsContext* graphics_context = nullptr;
	AsyncTransferQueue* async_transfer_queue = nullptr;
	
	FixedCountArray<NativeBufferResource, number_of_frames_in_flight> upload_buffers;
	FixedCountArray<u8*, number_of_frames_in_flight> upload_buffer_cpu_addresses;
	u64 upload_buffer_index = 0;
	
	NativeBufferResource mesh_asset_buffer;
	u64 mesh_asset_buffer_offset = 0;
	u64 mesh_asset_buffer_size   = 0;
	
	DebugGeometryBuffer debug_geometry_buffer;
};

RendererContext* CreateRendererContext(StackAllocator* alloc);
void ReleaseRendererContext(RendererContext* context, StackAllocator* alloc);

VirtualResourceTable* CreateResourceTable(StackAllocator* alloc);
void ReleaseResourceTable(GraphicsContext* graphics_context, VirtualResourceTable* resource_table);

RecordContext* BeginRecordContext(StackAllocator* alloc, RendererContext* context, WindowSwapChain* swap_chain, VirtualResourceTable* resource_table);
void BuildRenderPassesForFrame(RendererContext* renderer_context, RecordContext* record_context, EntitySystem* entity_system, u64 world_entity_guid);
