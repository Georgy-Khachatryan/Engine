#include "EntitySystem.h"
#include "Basic/BasicString.h"

extern ArrayView<EntityTypeInfo>      entity_type_info_table;
extern ArrayView<EntityQueryTypeInfo> entity_query_type_info_table;
extern ArrayView<ComponentTypeInfo>   component_type_info_table;

void InitializeEntitySystem(EntitySystem& system) {
	system.heap = CreateHeapAllocator(2 * 1024 * 1024);
	
	Array<EntityTypeArray> entity_type_arrays;
	ArrayResize(entity_type_arrays, &system.heap, entity_type_info_table.count);
	
	for (u32 entity_type_index = 0; entity_type_index < entity_type_info_table.count; entity_type_index += 1) {
		auto& array     = entity_type_arrays[entity_type_index];
		auto& type_info = entity_type_info_table[entity_type_index];
		
		Array<u8*> component_streams;
		ArrayResize(component_streams, &system.heap, type_info.component_type_ids.count);
		
		array.component_streams    = component_streams;
		array.entity_type_id.index = entity_type_index;
	}
	
	system.entity_type_arrays = entity_type_arrays;
}

template<typename Lambda>
static void IterateComponentStreams(ArrayView<u8*> component_streams, ArrayView<ComponentTypeID> component_type_ids, Lambda&& lambda) {
	for (u32 component_stream_index = 0; component_stream_index < component_type_ids.count; component_stream_index += 1) {
		auto component_type_id = component_type_ids[component_stream_index];
		lambda(component_streams[component_stream_index], component_type_info_table[component_type_id.index]);
	}
}

static void AllocateComponentStreams(ArrayView<u8*> component_streams, ArrayView<ComponentTypeID> component_type_ids, HeapAllocator& heap, u32 old_capacity, u32 new_capacity) {
	IterateComponentStreams(component_streams, component_type_ids, [&](u8*& stream, ComponentTypeInfo type_info) {
		stream = (u8*)heap.Reallocate(stream, old_capacity * type_info.size_bytes, new_capacity * type_info.size_bytes, 64u);
	});
}

static void MemsetComponentStreams(ArrayView<u8*> component_streams, ArrayView<ComponentTypeID> component_type_ids, u32 stream_offset, u32 entity_count, u8 pattern) {
	IterateComponentStreams(component_streams, component_type_ids, [&](u8* stream, ComponentTypeInfo type_info) {
		memset(stream + stream_offset * type_info.size_bytes, pattern, entity_count * type_info.size_bytes);
	});
}

static void MemcpyComponentStreams(ArrayView<u8*> component_streams, ArrayView<ComponentTypeID> component_type_ids, u32 dst_index, u32 src_index) {
	IterateComponentStreams(component_streams, component_type_ids, [&](u8* stream, ComponentTypeInfo type_info) {
		memcpy(stream + dst_index * type_info.size_bytes, stream + src_index * type_info.size_bytes, type_info.size_bytes);
	});
}

CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, EntityTypeID entity_type_id, u32 entity_count) {
	compile_const u32 base_allocation_count = 1024;
	
	auto& array = system.entity_type_arrays[entity_type_id.index];
	auto& type_info = entity_type_info_table[entity_type_id.index];
	
	if (array.count + entity_count > array.capacity) {
		u32 old_capacity = array.capacity;
		u32 new_capacity = AlignUp(old_capacity ? old_capacity * 3 / 2 + entity_count : entity_count, base_allocation_count);
		
		array.entity_id_to_stream_index = (u32*)system.heap.Reallocate(array.entity_id_to_stream_index, old_capacity * sizeof(u32), new_capacity * sizeof(u32), 64u);
		array.stream_index_to_entity_id = (u32*)system.heap.Reallocate(array.stream_index_to_entity_id, old_capacity * sizeof(u32), new_capacity * sizeof(u32), 64u);
		
		AllocateComponentStreams(array.component_streams, type_info.component_type_ids, system.heap, old_capacity, new_capacity);
		
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
	
	MemsetComponentStreams(array.component_streams, type_info.component_type_ids, stream_offset, entity_count, 0xCC);
	
	CreateEntitiesResult result;
	result.entity_ids     = entity_ids;
	result.stream_offset  = stream_offset;
	result.entity_type_id = entity_type_id;
	
	return result;
}

void RemoveEntity(EntitySystem& system, EntityTypeID entity_type_id, u32 entity_id) {
	auto& array = system.entity_type_arrays[entity_type_id.index];
	auto& type_info = entity_type_info_table[entity_type_id.index];
	
	u32 stream_index = array.entity_id_to_stream_index[entity_id];
	array.entity_id_to_stream_index[entity_id] = u32_max;
	
	if (array.count >= 2) {
		u32 back_stream_index = array.count - 1;
		u32 back_entity_id = array.stream_index_to_entity_id[back_stream_index];
		
		array.stream_index_to_entity_id[stream_index] = back_entity_id;
		array.entity_id_to_stream_index[back_entity_id] = stream_index;
		array.stream_index_to_entity_id[back_stream_index] = entity_id;
		
		MemcpyComponentStreams(array.component_streams, type_info.component_type_ids, stream_index, back_stream_index);
	}
	
	array.count -= 1;
}


ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system, EntityQueryTypeID query_type_id) {
	Array<EntityTypeArray*> result;
	
	auto match_key = entity_query_type_info_table[query_type_id.index].component_type_ids;
	
	u32 match_key_size = (u32)match_key.count;
	for (u32 entity_type_index = 0; entity_type_index < entity_type_info_table.count; entity_type_index += 1) {
		auto array_key = entity_type_info_table[entity_type_index].component_type_ids;
		u32 array_key_size = (u32)array_key.count;
		
		u32 array_key_index = 0;
		u32 match_key_index = 0;
		u32 match_count = 0;
		while (array_key_index < array_key_size && match_key_index < match_key_size) {
			u32 array_component_index = array_key[array_key_index].index;
			u32 match_component_index = match_key[match_key_index].index;
			
			if (array_component_index < match_component_index) {
				array_key_index += 1;
			} else if (match_component_index < array_component_index) {
				match_key_index += 1; // Skipping match key, could early out. match_count < match_key_size is guaranteed.
			} else {
				match_count += 1;
				array_key_index += 1;
				match_key_index += 1;
			}
		}
		
		if (match_count == match_key_size) {
			auto& array = system.entity_type_arrays[entity_type_index];
			array.entity_type_id.index = entity_type_index;
			ArrayAppend(result, alloc, &array);
		}
	}
	
	return result;
}

void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<u8*> output_component_streams) {
	auto array_key = entity_type_info_table[array->entity_type_id.index].component_type_ids;
	auto match_key = entity_query_type_info_table[query_type_id.index].component_type_ids;
	
	auto stream_indices = entity_query_type_info_table[query_type_id.index].component_stream_indices;
	
	u32 array_key_size = (u32)array_key.count;
	u32 match_key_size = (u32)match_key.count;
	
	u32 array_key_index = 0;
	u32 match_key_index = 0;
	while (array_key_index < array_key_size && match_key_index < match_key_size) {
		u32 array_component_index = array_key[array_key_index].index;
		u32 match_component_index = match_key[match_key_index].index;
		
		if (array_component_index < match_component_index) {
			array_key_index += 1;
		} else if (match_component_index < array_component_index) {
			match_key_index += 1; // Shouldn't ever happen if QueryEntities is correct.
		} else {
			output_component_streams[stream_indices[match_key_index]] = array->component_streams[array_key_index];
			array_key_index += 1;
			match_key_index += 1;
		}
	}
}
