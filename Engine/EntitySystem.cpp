#include "EntitySystem.h"
#include "Basic/BasicString.h"


static u64 ComputeHash(const EntityTypeKey& key) { return ComputeHash((u8*)key.active_component_mask.data, key.key_size_bytes); }

static u32 AddOrFindEntityTypeArrayByKey(EntitySystem& system, const EntityTypeKey& key) {
	auto [element, is_inserted] = HashTableAddOrFind(system.entity_type_key_to_entity_type_index, &system.heap, key, 0u);
	if (is_inserted) {
		element->value = (u32)system.entity_type_arrays.count;
		
		auto& array = ArrayEmplace(system.entity_type_arrays, &system.heap);
		array.key = key;
		array.entity_type_index = element->value;
		
		u32 stream_count = 0;
		for (u32 dword : key.active_component_mask) {
			stream_count += CountSetBits32(dword);
		}
		array.streams.count = stream_count;
	}
	return element->value;
}

extern ComponentTypeInfo component_type_infos[component_type_count];


template<typename Lambda>
static void IterateComponentStreams(ArrayView<u8*> streams, EntityTypeKey key, Lambda&& lambda) {
	u32 bit_offset = 0;
	u32 stream_id  = 0;
	for (u32 dword : key.active_component_mask) {
		for (u32 bit_index : BitScanLow32(dword)) {
			lambda(streams[stream_id], component_type_infos[bit_index + bit_offset]);
			stream_id += 1;
		}
		bit_offset += 32;
	}
}

static void AllocateComponentStreams(ArrayView<u8*> streams, EntityTypeKey key, HeapAllocator& heap, u32 old_capacity, u32 new_capacity) {
	IterateComponentStreams(streams, key, [&](u8*& stream, ComponentTypeInfo type_info) {
		stream = (u8*)heap.Reallocate(stream, old_capacity * type_info.size_bytes, new_capacity * type_info.size_bytes, Max(type_info.align_bytes, 64u));
	});
}

static void MemsetComponentStreams(ArrayView<u8*> streams, EntityTypeKey key, u32 stream_offset, u32 entity_count, u8 pattern) {
	IterateComponentStreams(streams, key, [&](u8* stream, ComponentTypeInfo type_info) {
		memset(stream + stream_offset * type_info.size_bytes, pattern, entity_count * type_info.size_bytes);
	});
}

static void MemcpyComponentStreams(ArrayView<u8*> streams, EntityTypeKey key, u32 dst_index, u32 src_index) {
	IterateComponentStreams(streams, key, [&](u8* stream, ComponentTypeInfo type_info) {
		memcpy(stream + dst_index * type_info.size_bytes, stream + src_index * type_info.size_bytes, type_info.size_bytes);
	});
}

CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, const EntityTypeKey& key, u32 entity_count) {
	compile_const u32 base_allocation_count = 1024;
	
	u32 entity_type_index = AddOrFindEntityTypeArrayByKey(system, key);
	auto& array = system.entity_type_arrays[entity_type_index];
	
	if (array.count + entity_count > array.capacity) {
		u32 old_capacity = array.capacity;
		u32 new_capacity = AlignUp(old_capacity ? old_capacity * 3 / 2 + entity_count : entity_count, base_allocation_count);
		
		array.entity_id_to_stream_index = (u32*)system.heap.Reallocate(array.entity_id_to_stream_index, old_capacity * sizeof(u32), new_capacity * sizeof(u32), 64u);
		array.stream_index_to_entity_id = (u32*)system.heap.Reallocate(array.stream_index_to_entity_id, old_capacity * sizeof(u32), new_capacity * sizeof(u32), 64u);
		
		AllocateComponentStreams(array.streams, key, system.heap, old_capacity, new_capacity);
		
		for (u32 i = old_capacity; i < new_capacity; i += 1) {
			array.stream_index_to_entity_id[i] = i;
		}
		
		array.capacity = new_capacity;
	}
	
	Array<u32> entity_ids;
	ArrayResize(entity_ids, alloc, entity_count);
	
	u32 stream_offset = array.count;
	array.count += entity_count;
	
	for (u32 i = 0; i < entity_count; i += 1) {
		u32 entity_id = array.stream_index_to_entity_id[stream_offset + i];
		entity_ids[i] = entity_id;
		
		array.entity_id_to_stream_index[entity_id] = stream_offset + i;
	}
	
	MemsetComponentStreams(array.streams, key, stream_offset, entity_count, 0xCC);
	
	CreateEntitiesResult result;
	result.entity_ids = entity_ids;
	result.stream_offset = stream_offset;
	result.entity_type_index = entity_type_index;
	
	return result;
}

void RemoveEntity(EntitySystem& system, u32 entity_type_index, u32 entity_id) {
	auto& array = system.entity_type_arrays[entity_type_index];
	
	u32 stream_index = array.entity_id_to_stream_index[entity_id];
	array.entity_id_to_stream_index[entity_id] = u32_max;
	
	if (array.count >= 2) {
		u32 back_stream_index = array.count - 1;
		u32 back_entity_id = array.stream_index_to_entity_id[back_stream_index];
		
		array.stream_index_to_entity_id[stream_index] = back_entity_id;
		array.entity_id_to_stream_index[back_entity_id] = stream_index;
		array.stream_index_to_entity_id[back_stream_index] = entity_id;
		
		MemcpyComponentStreams(array.streams, array.key, stream_index, back_stream_index);
	}
	
	array.count -= 1;
}


ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system, const EntityTypeKey& key) {
	Array<EntityTypeArray*> result;
	
	for (auto [type_key, entity_type_index] : system.entity_type_key_to_entity_type_index) {
		bool all_match = true;
		for (u64 i = 0; i < EntityTypeKey::key_size_dwords; i += 1) {
			u32 target_dword = key.active_component_mask[i];
			bool match = (type_key.active_component_mask[i] & target_dword) == target_dword;
			all_match &= match;
		}
		if (all_match == false) continue;
		
		ArrayAppend(result, alloc, &system.entity_type_arrays[entity_type_index]);
	}
	
	return result;
}

void ExtractComponentStreams(EntityTypeArray* array, const EntityTypeKey& output_key, ArrayView<u8*> output_streams) {
	u32 bit_offset = 0;
	u32 stream_id  = 0;
	u32 output_stream_id = 0;
	for (u64 i = 0; i < EntityTypeKey::key_size_dwords; i += 1) {
		u32 dword        = array->key.active_component_mask[i];
		u32 output_dword = output_key.active_component_mask[i];
		
		for (u32 bit_index : BitScanLow32(dword)) {
			if (output_dword & (1u << bit_index)) {
				output_streams[output_stream_id] = array->streams[stream_id];
				output_stream_id += 1;
			}
			stream_id += 1;
		}
		bit_offset += 32;
	}
}
