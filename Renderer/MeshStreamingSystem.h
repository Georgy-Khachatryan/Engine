#pragma once
#include "Basic/Basic.h"

struct AssetEntitySystem;
struct AsyncTransferQueue;
struct GpuReadbackQueue;
struct MeshStreamingSystem;
struct RecordContext;
struct ThreadPool;

MeshStreamingSystem* CreateMeshStreamingSystem(StackAllocator* alloc, u64 buffer_size);
void UpdateMeshStreamingSystem(MeshStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* mesh_streaming_feedback_queue);
void UpdateMeshStreamingFiles(MeshStreamingSystem* system, ThreadPool* thread_pool, RecordContext* record_context, AssetEntitySystem* asset_system);
