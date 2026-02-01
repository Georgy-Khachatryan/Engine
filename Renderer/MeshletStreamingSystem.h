#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"

enum struct MeshletPageTableUpdateCommandType : u32;
struct AssetEntitySystem;
struct AsyncTransferQueue;
struct GpuReadbackQueue;
struct MeshletStreamingSystem;
struct RecordContext;

struct MeshletRuntimePageUpdateCommand {
	u32 mesh_asset_index = 0;
	u32 asset_page_index = 0;
	u32 runtime_page_index = 0;
	MeshletPageTableUpdateCommandType type = (MeshletPageTableUpdateCommandType)0;
};

MeshletStreamingSystem* CreateMeshletStreamingSystem(StackAllocator* alloc, u64 buffer_size);
void UpdateMeshletStreamingSystem(MeshletStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* meshlet_streaming_feedback_queue);
void UpdateMeshletStreamingFiles(MeshletStreamingSystem* system, StackAllocator* alloc, AssetEntitySystem* asset_system);
ArrayView<MeshletRuntimePageUpdateCommand> GetPageTableUpdateCommands(MeshletStreamingSystem* system);
