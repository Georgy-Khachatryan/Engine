#include "Basic/Basic.h"
#include "Basic/BasicBitArray.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "MeshAsset.h"
#include "Renderer.h"
#include "RendererEntities.h"
#include "RenderPasses.h"

extern MeshImportResult ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid);

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
			auto& aabb = streams.aabb[i];
			
			if (layout.version == MeshRuntimeDataLayout::current_version) continue;
			
			if (layout.file_guid == 0) {
				layout.file_guid = GenerateRandomNumber64(asset_system.guid_random_seed);
			}
			
			auto result = ImportFbxMeshFile(alloc, streams.source_data[i].filepath, layout.file_guid);
			layout   = result.layout;
			aabb.min = result.aabb_min;
			aabb.max = result.aabb_max;
		}
	}
	
	for (auto* entity_array : entity_view) {
		ProfilerScope("MeshAssetTypeGpuComponentUpdate");
		
		u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count == 0) continue;
		
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto& layout = streams.runtime_data_layout[i];
			auto& allocation = streams.allocation[i];
			
			if (allocation.base_offset != u32_max) continue;
			
			compile_const u32 page_residency_mask_size = (MeshletPageHeader::max_page_count / 32u) * sizeof(u32);
			u32 file_data_size  = layout.meshlet_group_count * sizeof(MeshletGroup) + page_residency_mask_size;
			u32 allocation_size = file_data_size + layout.page_count * sizeof(u32);
			
			u32 aligned_allocation_size = AlignUp(allocation_size, 4096u);
			
			u32 allocation_offset = (u32)mesh_asset_buffer_offset;
			mesh_asset_buffer_offset += aligned_allocation_size;
			
			allocation.base_offset = allocation_offset;
			
			auto file = SystemOpenFile(alloc, StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, layout.file_guid), OpenFileFlags::Read | OpenFileFlags::Async);
			streams.runtime_file[i].file = file;
			
			u64 file_offset = layout.page_count * MeshletPageHeader::page_size;
			AsyncCopyFileToBuffer(async_transfer_queue, mesh_asset_buffer, allocation_offset, mesh_asset_buffer_size, file, file_offset, AlignUp(file_data_size, 4096u));
		}
		
		auto gpu_mesh_asset_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_mesh_asset_data);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto& layout = streams.runtime_data_layout[i];
			auto& allocation = streams.allocation[i];
			auto& aabb = streams.aabb[i];
			
			GpuMeshAssetData mesh_asset;
			mesh_asset.meshlet_group_buffer_offset = allocation.base_offset;
			mesh_asset.meshlet_group_count = (u16)layout.meshlet_group_count;
			mesh_asset.meshlet_page_count  = (u16)layout.page_count;
			mesh_asset.aabb_center = (aabb.max + aabb.min) * 0.5f;
			mesh_asset.aabb_radius = (aabb.max - aabb.min) * 0.5f;
			AppendGpuTransferCommand(gpu_mesh_asset_data, i, mesh_asset);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_mesh_asset_data);
	}
	
	renderer_context->mesh_asset_buffer_offset = mesh_asset_buffer_offset;
}
