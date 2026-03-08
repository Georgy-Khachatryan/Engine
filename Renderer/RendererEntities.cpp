#include "Basic/Basic.h"
#include "Basic/BasicBitArray.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/RecordContext.h"
#include "MaterialAsset.h"
#include "MeshAsset.h"
#include "Renderer.h"
#include "RendererEntities.h"
#include "RenderPasses.h"
#include "TextureAsset.h"

void UpdateRendererEntityGpuComponents(StackAllocator* alloc, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
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
	
	auto texture_streams = ExtractComponentStreams<TextureAssetType>(QueryEntityTypeArray<TextureAssetType>(asset_system));
	for (auto* entity_array : QueryEntities<MaterialAssetType>(alloc, asset_system)) {
		ProfilerScope("MaterialAssetTypeGpuComponentUpdate");
		
		u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count == 0) continue;
		
		auto find_texture_index = [&](TextureAssetGUID texture_asset_guid)-> u32 {
			auto* element = HashTableFind(asset_system.entity_guid_to_entity_id, texture_asset_guid.guid);
			return element ? texture_streams.descriptor_allocation[element->value.entity_id.index].index : u32_max;
		};
		
		auto streams = ExtractComponentStreams<MaterialAssetType>(entity_array);
		auto gpu_texture_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_texture_data);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto& texture_data = streams.texture_data[i];
			
			GpuMaterialTextureData gpu_data;
			gpu_data.albedo = find_texture_index(texture_data.albedo);
			gpu_data.normal = find_texture_index(texture_data.normal);
			AppendGpuTransferCommand(gpu_texture_data, i, gpu_data);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_texture_data);
	}
}

void ReleaseTextureAssets(StackAllocator* alloc, GraphicsContext* graphics_context, AssetEntitySystem& asset_system) {
	for (auto* entity_array : QueryEntities<TextureAssetType>(alloc, asset_system)) {
		auto streams = ExtractComponentStreams<TextureAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->alive_mask)) {
			u32 index = streams.descriptor_allocation[i].index;
			if (index != u32_max) DeallocatePersistentSrvDescriptor(graphics_context, index);
			
			auto resource = streams.resource_allocation[i].resource;
			if (resource.handle != nullptr) ReleaseTextureResource(graphics_context, resource);
		}
	}
}
