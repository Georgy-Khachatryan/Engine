#include "Entities.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicSaveLoad.h"
#include "Renderer/RenderPasses.h"

void UpdateEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateEntityGpuComponents");
	
	for (auto* entity_array : QueryEntities<MeshEntityType>(alloc, world_system)) {
		ProfilerScope("MeshEntityGpuComponentUpdate");
		
		u64 dirty_qword_count = 0;
		u64 dirty_entity_count = BitArrayCountSetBitsAndNonZeroQwords(entity_array->dirty_mask, dirty_qword_count);
		if (dirty_entity_count == 0) continue;
		
		auto streams = ExtractComponentStreams<MeshEntityType>(entity_array);
		
		auto alive_mask = AllocateGpuComponentUploadBuffer(record_context, dirty_qword_count, streams.alive_mask);
		for (u64 i = 0; i < entity_array->dirty_mask.count; i += 1) {
			if (entity_array->dirty_mask[i] != 0) {
				AppendGpuTransferCommand(alive_mask, i, AliveEntityMask{ entity_array->alive_mask[i] });
			}
		}
		ArrayAppend(gpu_uploads, alloc, alive_mask);
		
		auto gpu_transform = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_transform);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			GpuTransform transform;
			transform.position = streams.position[i].position;
			transform.scale    = streams.scale[i].scale;
			transform.rotation = streams.rotation[i].rotation;
			AppendGpuTransferCommand(gpu_transform, i, transform);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_transform);
		
		auto gpu_mesh_entity_data = AllocateGpuComponentUploadBuffer(record_context, dirty_entity_count, streams.gpu_mesh_entity_data);
		for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
			auto* element = HashTableFind(asset_system.entity_guid_to_entity_id, streams.mesh_asset[i].guid);
			
			GpuMeshEntityData mesh_entity;
			mesh_entity.mesh_asset_index = element ? element->value.entity_id.index : u32_max;
			AppendGpuTransferCommand(gpu_mesh_entity_data, i, mesh_entity);
		}
		ArrayAppend(gpu_uploads, alloc, gpu_mesh_entity_data);
	}
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

void ReleaseEntityComponents(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system) {
	ProfilerScope("ReleaseEntityComponents");
	
	ReleaseNameComponents(alloc, world_system);
	ReleaseNameComponents(alloc, asset_system);
}

static void SaveLoad(SaveLoadBuffer& buffer, HashTableElement<u64, void>& element, u64 version) {
	SaveLoad(buffer, element.key);
}

void SaveLoad(SaveLoadBuffer& buffer, EditorSelectionState& data, u64 version) {
	SaveLoad(buffer, data.selected_entities_hash_table);
}
