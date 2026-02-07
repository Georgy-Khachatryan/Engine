#include "Basic/Basic.h"
#include "Basic/BasicBitArray.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/RecordContext.h"
#include "MeshAsset.h"
#include "TextureAsset.h"
#include "Renderer.h"
#include "RendererEntities.h"
#include "RenderPasses.h"

void UpdateRendererEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateRendererEntityGpuComponents");
	
	for (auto* entity_array : QueryEntities<MeshAssetType>(alloc, asset_system)) {
		ProfilerScope("MeshAssetTypeGpuComponentUpdate");
		
		u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count == 0) continue;
		
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		auto gpu_mesh_asset_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_mesh_asset_data);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto& layout = streams.runtime_data_layout[i];
			auto& allocation = streams.allocation[i];
			auto& aabb = streams.aabb[i];
			
			GpuMeshAssetData mesh_asset;
			mesh_asset.meshlet_group_buffer_offset = allocation.offset;
			mesh_asset.meshlet_group_count = (u16)layout.meshlet_group_count;
			mesh_asset.meshlet_page_count  = (u16)layout.page_count;
			mesh_asset.aabb_center = (aabb.max + aabb.min) * 0.5f;
			mesh_asset.aabb_radius = (aabb.max - aabb.min) * 0.5f;
			mesh_asset.rcp_quantization_scale = layout.rcp_quantization_scale;
			AppendGpuTransferCommand(gpu_mesh_asset_data, i, mesh_asset);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_mesh_asset_data);
	}
	
	auto* graphics_context = record_context->context;
	for (auto* entity_array : QueryEntities<TextureAssetType>(alloc, asset_system)) {
		ProfilerScope("MeshAssetTypeGpuComponentUpdate");
		
		auto streams = ExtractComponentStreams<TextureAssetType>(entity_array);
		for (u64 i : BitArrayIt(entity_array->created_mask)) {
			streams.descriptor_allocation[i].index = AllocatePersistentSrvDescriptor(graphics_context);
		}
		
		for (u64 i : BitArrayIt(entity_array->removed_mask)) {
			u32 index = streams.descriptor_allocation[i].index;
			if (index != u32_max) DeallocatePersistentSrvDescriptor(graphics_context, index);
		}
	}
}
