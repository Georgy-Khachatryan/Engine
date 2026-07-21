#include "Entities.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicSaveLoad.h"
#include "Renderer/RenderPasses.h"

void UpdateEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateEntityGpuComponents");
	
	auto mesh_asset_streams = ExtractComponentStreams<MeshAssetType>(QueryEntityTypeArray<MeshAssetType>(asset_system));
	for (auto* entity_array : QueryEntities<MeshEntityType>(alloc, world_system)) {
		ProfilerScope("MeshEntityGpuComponentUpdate");
		auto streams = ExtractComponentStreams<MeshEntityType>(entity_array);
		
		auto dirty_mask      = entity_array->dirty_mask;
		auto prev_dirty_mask = entity_array->prev_dirty_mask;
		auto dirty_or_prev_dirty_mask = ArrayViewAllocate<u64>(alloc, dirty_mask.count);
		
		u64 dirty_or_prev_dirty_count = 0;
		for (u64 qword_index = 0; qword_index < dirty_mask.count; qword_index += 1) {
			u64 dirty_or_prev_dirty_qword = dirty_mask[qword_index] | prev_dirty_mask[qword_index];
			dirty_or_prev_dirty_mask[qword_index] = dirty_or_prev_dirty_qword;
			dirty_or_prev_dirty_count += CountSetBits(dirty_or_prev_dirty_qword);
		}
		
		if (dirty_or_prev_dirty_count != 0) {
			auto created_mask = entity_array->created_mask;
			
			auto gpu_transform = AllocateGpuComponentUploadBuffer(record_context, dirty_or_prev_dirty_count, streams.gpu_transform, streams.prev_gpu_transform);
			for (u64 i : BitArrayIt(dirty_or_prev_dirty_mask)) {
				bool is_created = BitArrayTestBit(created_mask, i);
				
				GpuTransform transform;
				transform.position = streams.position[i].position;
				transform.scale    = streams.scale[i].scale;
				transform.rotation = streams.rotation[i].rotation;
				AppendGpuTransferCommand(gpu_transform, i, transform, is_created ? GpuComponentUpdateFlags::InitHistory : GpuComponentUpdateFlags::CopyHistory);
			}
			ArrayAppend(gpu_uploads, alloc, gpu_transform);
		}
		
		u64 dirty_entity_count = BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count != 0) {
			auto gpu_mesh_entity_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_mesh_entity_data);
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				auto* mesh_asset = HashTableFind(asset_system.entity_guid_to_entity_id, streams.mesh_asset[i].guid);
				
				GpuMeshEntityData mesh_entity;
				mesh_entity.mesh_asset_index = mesh_asset ? mesh_asset->value.entity_id.index : u32_max;
				
				u64 material_asset_guid = streams.material_asset[i].guid;
				if (mesh_entity.mesh_asset_index != u32_max && material_asset_guid == 0) {
					material_asset_guid = mesh_asset_streams.material_asset[mesh_entity.mesh_asset_index].guid;
				}
				
				auto* material_asset = HashTableFind(asset_system.entity_guid_to_entity_id, material_asset_guid);
				mesh_entity.material_asset_index = material_asset ? material_asset->value.entity_id.index : u32_max;
				
				AppendGpuTransferCommand(gpu_mesh_entity_data, i, mesh_entity);
			}
			ArrayAppend(gpu_uploads, alloc, gpu_mesh_entity_data);
		}
	}
	
	for (auto* entity_array : QueryEntities<LightEntityType>(alloc, world_system)) {
		ProfilerScope("LightEntityGpuComponentUpdate");
		auto streams = ExtractComponentStreams<LightEntityType>(entity_array);
		
		u64 dirty_entity_count = BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count != 0) {
			auto gpu_light_entity_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_light_entity_data);
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				auto& light_component = streams.light[i];
				
				float outer_attenuation_radius = light_component.attenuation_radius;
				float inner_attenuation_radius = light_component.attenuation_radius * (1.f - Math::Max(light_component.attenuation_radius_falloff, 1.f / 1024.f));
				
				float cos_outer_attenuation_angle = cosf(light_component.attenuation_angle * 0.5f);
				float cos_inner_attenuation_angle = cosf(light_component.attenuation_angle * 0.5f * (1.f - Math::Max(light_component.attenuation_angle_falloff, 1.f / 1024.f)));
				
				GpuLightEntityData light_entity;
				light_entity.light_position         = streams.position[i].position;
				light_entity.light_direction        = streams.rotation[i].rotation * float3(0.f, 0.f, 1.f);
				light_entity.color                  = Math::DecodeSRGB(light_component.color);
				light_entity.radiance_or_irradiance = light_component.radiance_or_irradiance;
				light_entity.type                   = light_component.type;
				light_entity.light_radius           = light_component.radius;
				light_entity.distance_attenuation   = Math::SmoothStepCoefficients(outer_attenuation_radius, inner_attenuation_radius);
				light_entity.angle_attenuation      = Math::SmoothStepCoefficients(cos_outer_attenuation_angle, cos_inner_attenuation_angle);
				light_entity.attenuation_radius     = outer_attenuation_radius;
				light_entity.tan_attenuation_angle  = tanf(light_component.attenuation_angle * 0.5f);
				light_entity.cos_attenuation_angle  = cos_outer_attenuation_angle;
				
				AppendGpuTransferCommand(gpu_light_entity_data, i, light_entity);
			}
			ArrayAppend(gpu_uploads, alloc, gpu_light_entity_data);
		}
	}
	
	UpdateEntityAliveMasks(alloc, record_context, world_system, gpu_uploads);
}

static void ReleaseNameComponents(StackAllocator* alloc, EntitySystemBase& entity_system) {
	ProfilerScope("ReleaseNameComponents");
	
	for (auto* entity_array : QueryEntities<NameQuery>(alloc, entity_system)) {
		auto streams = ExtractComponentStreams<NameQuery>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->removed_mask)) {
			entity_system.heap.Deallocate(streams.name[i].name.data);
			streams.name[i] = {};
		}
	}
}

void ReleaseAssetComponents(StackAllocator* alloc, AssetEntitySystem& asset_system) {
	ProfilerScope("ReleaseAssetComponents");
	
	ReleaseNameComponents(alloc, asset_system);
}

void ReleaseEntityComponents(StackAllocator* alloc, WorldEntitySystem& world_system) {
	ProfilerScope("ReleaseEntityComponents");
	
	ReleaseNameComponents(alloc, world_system);
}
