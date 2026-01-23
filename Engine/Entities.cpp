#include "Entities.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicSaveLoad.h"
#include "Renderer/RenderPasses.h"

void UpdateEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads) {
	ProfilerScope("UpdateEntityGpuComponents");
	
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
		
		
		auto created_mask = entity_array->created_mask;
		auto removed_mask = entity_array->removed_mask;
		auto created_or_removed_mask = ArrayViewAllocate<u64>(alloc, created_mask.count);
		
		u64 created_or_removed_qword_count = 0;
		for (u64 qword_index = 0; qword_index < dirty_mask.count; qword_index += 1) {
			u64 created_or_removed_qword = created_mask[qword_index] | removed_mask[qword_index];
			created_or_removed_mask[qword_index] = created_or_removed_qword;
			created_or_removed_qword_count += created_or_removed_qword != 0 ? 1 : 0;
		}
		
		if (created_or_removed_qword_count) {
			auto gpu_alive_mask = AllocateGpuComponentUploadBuffer(record_context, created_or_removed_qword_count, streams.alive_mask);
			for (u64 qword_index = 0; qword_index < created_or_removed_mask.count; qword_index += 1) {
				if (created_or_removed_mask[qword_index] != 0) {
					AppendGpuTransferCommand(gpu_alive_mask, qword_index, AliveEntityMask{ entity_array->alive_mask[qword_index] });
				}
			}
			ArrayAppend(gpu_uploads, alloc, gpu_alive_mask);
		}
		
		u64 dirty_entity_count = BitArrayCountSetBits(entity_array->dirty_mask);
		if (dirty_entity_count != 0) {
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
