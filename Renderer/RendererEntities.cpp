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

void UpdateEntityAliveMasks(StackAllocator* alloc, RecordContext* record_context, EntitySystemBase& entity_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateEntityAliveMasks");
	
	for (auto* entity_array : QueryEntities<AliveEntityMaskQuery>(alloc, entity_system)) {
		if (entity_array->count == 0) continue;
		
		auto created_mask = entity_array->created_mask;
		auto removed_mask = entity_array->removed_mask;
		auto created_or_removed_mask = ArrayViewAllocate<u64>(alloc, created_mask.count);
		
		u64 created_or_removed_qword_count = 0;
		for (u64 qword_index = 0; qword_index < created_mask.count; qword_index += 1) {
			u64 created_or_removed_qword = created_mask[qword_index] | removed_mask[qword_index];
			created_or_removed_mask[qword_index] = created_or_removed_qword;
			created_or_removed_qword_count += created_or_removed_qword != 0 ? 1 : 0;
		}
		
		if (created_or_removed_qword_count != 0) {
			auto streams = ExtractComponentStreams<AliveEntityMaskQuery>(entity_array);
			auto gpu_alive_mask = AllocateGpuComponentUploadBuffer(record_context, created_or_removed_qword_count, streams.alive_mask);
			
			for (u64 qword_index = 0; qword_index < created_or_removed_mask.count; qword_index += 1) {
				if (created_or_removed_mask[qword_index] != 0) {
					AppendGpuTransferCommand(gpu_alive_mask, qword_index, AliveEntityMask{ entity_array->alive_mask[qword_index] });
				}
			}
			ArrayAppend(gpu_uploads, alloc, gpu_alive_mask);
		}
	}
}

void UpdateRendererAssetGpuComponents(StackAllocator* alloc, RecordContext* record_context, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateRendererAssetGpuComponents");
	
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
	
	for (auto* entity_array : QueryEntities<TextureAssetType>(alloc, asset_system)) {
		ProfilerScope("TextureAssetTypeGpuComponentUpdate");
		auto streams = ExtractComponentStreams<TextureAssetType>(entity_array);
		
		auto* resource_table = record_context->resource_table;
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto& descriptor_allocation = streams.descriptor_allocation[i];
			
			auto texture_descriptor = HLSL::Texture2D<float4>((VirtualResourceID)0);
			if (descriptor_allocation.mip_level_mask != 0) {
				auto texture_id = resource_table->AddTransient(streams.resource_allocation[i].resource, streams.runtime_data_layout[i].size);
				texture_descriptor = HLSL::Texture2D<float4>(texture_id, FirstBitLow32(descriptor_allocation.mip_level_mask));
			}
			
			// Perfectly descriptor updates should be staged similar to the regular GPU component
			// updates, but D3D12 doesn't support updating descriptors in the GPU timeline.
			CreateResourceDescriptor(record_context, texture_descriptor, descriptor_allocation.index);
		}
	}
	
	auto texture_streams = ExtractComponentStreams<TextureAssetType>(QueryEntityTypeArray<TextureAssetType>(asset_system));
	for (auto* entity_array : QueryEntities<MaterialAssetType>(alloc, asset_system)) {
		ProfilerScope("MaterialAssetTypeGpuComponentUpdate");
		
		u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count == 0) continue;
		
		auto find_texture_index = [&](TextureAssetGUID texture_asset_guid, u32 default_value)-> u32 {
			auto* element = HashTableFind(asset_system.entity_guid_to_entity_id, texture_asset_guid.guid);
			if (element) return texture_streams.descriptor_allocation[element->value.entity_id.index].index;
			return default_value | (u32)MaterialTextureIndexFlags::UseDefault;
		};
		
		auto streams = ExtractComponentStreams<MaterialAssetType>(entity_array);
		auto gpu_texture_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_texture_data);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto& texture_data = streams.texture_data[i];
			
			GpuMaterialTextureData gpu_data;
			gpu_data.albedo    = find_texture_index(texture_data.albedo,    texture_data.default_albedo);
			gpu_data.normal    = find_texture_index(texture_data.normal,    0);
			gpu_data.roughness = find_texture_index(texture_data.roughness, texture_data.default_roughness);
			gpu_data.metalness = find_texture_index(texture_data.metalness, texture_data.default_metalness);
			AppendGpuTransferCommand(gpu_texture_data, i, gpu_data);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_texture_data);
	}
	
	UpdateEntityAliveMasks(alloc, record_context, asset_system, gpu_uploads);
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
