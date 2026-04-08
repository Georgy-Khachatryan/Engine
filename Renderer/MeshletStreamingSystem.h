#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "GraphicsApi/GraphicsApiTypes.h"

enum struct MeshletPageUpdateCommandType : u16;
struct AssetEntitySystem;
struct AsyncTransferQueue;
struct GpuReadbackQueue;
struct MeshletStreamingSystem;
struct RecordContext;

struct MeshletRuntimePageUpdateCommand {
	u32 mesh_asset_index   = 0;
	MeshletPageUpdateCommandType type = (MeshletPageUpdateCommandType)0;
	u16 readback_index     = 0;
	u16 asset_page_index   = 0;
	u16 runtime_page_index = 0;
};

struct MeshletStreamingUpdateCommands {
	ArrayView<MeshletRuntimePageUpdateCommand> page_table_update_commands;
	GpuAddress page_header_readback;
	u32 page_header_readback_count = 0;
};

struct MeshletRtasBuildCommand {
	BuildInputsMeshletRTAS inputs;
	u32 runtime_page_index = 0;
	u32 mesh_asset_index   = 0;
};

struct MeshletRtasBuildCommands {
	ArrayView<MeshletRtasBuildCommand> meshlet_rtas_build_commands;
	u32 vertex_buffer_scratch_offset = 0;
};

MeshletStreamingSystem* CreateMeshletStreamingSystem(StackAllocator* alloc, u32 meshlet_rtas_buffer_size);
void UpdateMeshletStreamingSystem(MeshletStreamingSystem* system, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem* asset_system, GpuReadbackQueue* meshlet_streaming_feedback_queue);
MeshletStreamingUpdateCommands GetMeshletStreamingUpdateCommands(MeshletStreamingSystem* system);
MeshletRtasBuildCommands GetMeshletRtasBuildCommands(MeshletStreamingSystem* system);
