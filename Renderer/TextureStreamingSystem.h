#pragma once
#include "Basic/Basic.h"

struct AssetEntitySystem;
struct AsyncTransferQueue;
struct GpuReadbackQueue;
struct GraphicsContext;
struct RecordContext;
struct TextureStreamingSystem;
struct ThreadPool;

TextureStreamingSystem* CreateTextureStreamingSystem(GraphicsContext* context, StackAllocator* alloc, u64 heap_size);
void ReleaseTextureStreamingSystem(GraphicsContext* context, TextureStreamingSystem* system);

void UpdateTextureStreamingSystem(TextureStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* texture_streaming_feedback_queue);
void UpdateTextureStreamingFiles(TextureStreamingSystem* system, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system);

