#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"

enum struct MeshletPageTableUpdateCommandType : u32;
struct AssetEntitySystem;
struct AsyncTransferQueue;
struct GpuReadbackQueue;
struct MeshletStreamingSystem;
struct RecordContext;

struct MeshletStreamingPageTableUpdateCommand {
	u64 subresource_id = 0;
	MeshletPageTableUpdateCommandType type = (MeshletPageTableUpdateCommandType)0;
	u32 runtime_page_index = 0;
};

MeshletStreamingSystem* CreateMeshletStreamingSystem(StackAllocator* alloc, u64 buffer_size);
void UpdateMeshletStreamingSystem(MeshletStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* meshlet_streaming_feedback_queue);
void UpdateMeshletStreamingFiles(MeshletStreamingSystem* system, StackAllocator* alloc, AssetEntitySystem* asset_system);
ArrayView<MeshletStreamingPageTableUpdateCommand> GetPageTableUpdateCommands(MeshletStreamingSystem* system);
