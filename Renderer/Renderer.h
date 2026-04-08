#pragma once
#include "Basic/Basic.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct AssetEntitySystem;
struct AsyncTransferQueue;
struct GraphicsContext;
struct MeshletStreamingSystem;
struct MeshStreamingSystem;
struct TextureStreamingSystem;
struct ThreadPool;
struct VirtualResourceTable;
struct WindowSwapChain;
struct WorldEntitySystem;

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
	FixedCountArray<NativeBufferResource, number_of_frames_in_flight> readback_buffers;
	FixedCountArray<u8*, number_of_frames_in_flight> upload_buffer_cpu_addresses;
	FixedCountArray<u8*, number_of_frames_in_flight> readback_buffer_cpu_addresses;
	u64 transient_buffer_index = 0;
	
	MeshletStreamingSystem* meshlet_streaming_system = nullptr;
	MeshStreamingSystem*    mesh_streaming_system    = nullptr;
	TextureStreamingSystem* texture_streaming_system = nullptr;
	
	NativeBufferResource mesh_asset_buffer;
	u64 mesh_asset_buffer_size    = 0;
	u64 mesh_asset_buffer_address = 0;
	
	NativeBufferResource meshlet_rtas_buffer;
	u64 meshlet_rtas_buffer_size    = 0;
	u64 meshlet_rtas_buffer_address = 0;
	
	NativeBufferResource streaming_scratch_buffer;
	u64 streaming_scratch_buffer_size    = 0;
	u64 streaming_scratch_buffer_address = 0;
	
	DebugGeometryBuffer debug_geometry_buffer;
};

RendererContext* CreateRendererContext(StackAllocator* alloc);
void ReleaseRendererContext(RendererContext* context, StackAllocator* alloc);

VirtualResourceTable* CreateResourceTable(StackAllocator* alloc);
void ReleaseResourceTable(GraphicsContext* graphics_context, VirtualResourceTable* resource_table);

RecordContext* BeginRecordContext(StackAllocator* alloc, RendererContext* context, WindowSwapChain* swap_chain, VirtualResourceTable* resource_table);
void BuildRenderPassesForFrame(RendererContext* renderer_context, RecordContext* record_context, WorldEntitySystem* world_system, AssetEntitySystem* asset_system, u64 world_entity_guid);

void UpdateStreamingSystems(RendererContext* renderer_context, ThreadPool* thread_pool, RecordContext* record_context, WorldEntitySystem* world_system, AssetEntitySystem* asset_system, u64 world_entity_guid);
