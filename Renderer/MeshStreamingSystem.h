#pragma once
#include "Basic/Basic.h"

struct MeshStreamingSystem;
struct AsyncTransferQueue;
struct RecordContext;
struct AssetEntitySystem;
struct GpuReadbackQueue;

MeshStreamingSystem* CreateMeshStreamingSystem(StackAllocator* alloc, u64 buffer_size);
void UpdateMeshStreamingSystem(MeshStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* mesh_streaming_feedback_queue);
void UpdateMeshStreamingFiles(MeshStreamingSystem* system, RecordContext* record_context, AssetEntitySystem* asset_system);
