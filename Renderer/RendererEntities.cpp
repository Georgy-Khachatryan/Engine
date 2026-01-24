#include "Basic/Basic.h"
#include "Basic/BasicBitArray.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "MeshAsset.h"
#include "Renderer.h"
#include "RendererEntities.h"
#include "RenderPasses.h"

extern MeshRuntimeDataLayout ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid);

void UpdateRendererEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, RendererContext* renderer_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateRendererEntityGpuComponents");
	
	auto mesh_asset_buffer = renderer_context->mesh_asset_buffer;
	u64 mesh_asset_buffer_size = renderer_context->mesh_asset_buffer_size;
	u64 mesh_asset_buffer_offset = renderer_context->mesh_asset_buffer_offset;
	auto* async_transfer_queue = renderer_context->async_transfer_queue;
	
	auto entity_view = QueryEntities<MeshAssetType>(alloc, asset_system);
	
	for (auto* entity_array : entity_view) {
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		for (u64 i : BitArrayIt(entity_array->created_mask)) {
			auto& layout = streams.runtime_data_layout[i];
			if (layout.version == MeshRuntimeDataLayout::current_version) continue;
			
			if (layout.file_guid == 0) {
				layout.file_guid = GenerateRandomNumber64(asset_system.guid_random_seed);
			}
			
			auto& source_data = streams.source_data[i];
			layout = ImportFbxMeshFile(alloc, source_data.filepath, layout.file_guid);
		}
	}
	
	for (auto* entity_array : entity_view) {
		ProfilerScope("MeshAssetTypeGpuComponentUpdate");
		
		u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count == 0) continue;
		
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			if (streams.allocation[i].base_offset != u32_max) continue;
			
			u32 allocation_size   = AlignUp(streams.runtime_data_layout[i].AllocationSize(), 4096u);
			u32 allocation_offset = (u32)mesh_asset_buffer_offset;
			
			mesh_asset_buffer_offset += allocation_size;
			streams.allocation[i].base_offset = allocation_offset;
			
			u64 guid = streams.runtime_data_layout[i].file_guid;
			auto file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, guid), OpenFileFlags::Read | OpenFileFlags::Async);
			
			streams.runtime_file[i].file = file;
			
			AsyncCopyFileToBuffer(async_transfer_queue, mesh_asset_buffer, allocation_offset, mesh_asset_buffer_size, file, 0, allocation_size);
		}
		
		auto gpu_mesh_asset_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_mesh_asset_data);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto layout = streams.runtime_data_layout[i];
			u32 base_offset = streams.allocation[i].base_offset;
			
			GpuMeshAssetData mesh_asset;
			mesh_asset.page_buffer_offset  = base_offset + layout.PageBufferOffset();
			mesh_asset.meshlet_group_buffer_offset = base_offset + layout.MeshletGroupBufferOffset();
			mesh_asset.meshlet_group_count = layout.meshlet_group_count;
			AppendGpuTransferCommand(gpu_mesh_asset_data, i, mesh_asset);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_mesh_asset_data);
	}
	
	renderer_context->mesh_asset_buffer_offset = mesh_asset_buffer_offset;
}
