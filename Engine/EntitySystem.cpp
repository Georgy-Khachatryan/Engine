#include "EntitySystem.h"
#include "Basic/BasicSaveLoad.h"

extern ArrayView<EntityTypeInfo>      entity_type_info_table;
extern ArrayView<EntityQueryTypeInfo> entity_query_type_info_table;
extern ArrayView<ComponentTypeInfo>   component_type_info_table;
extern ArrayView<SaveLoadCallback>    component_save_load_callbacks;
extern ArrayView<DefaultInitializeCallback> component_default_initialize_callbacks;

compile_const u32 base_allocation_count = 1024;

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

static void DefaultInitializeStreams(ArrayView<u8*> component_streams, ArrayView<ComponentTypeID> component_type_ids, u64 begin, u64 end) {
	for (u32 component_stream_index = 0; component_stream_index < component_type_ids.count; component_stream_index += 1) {
		auto component_type_id = component_type_ids[component_stream_index];
		component_default_initialize_callbacks[component_type_id.index](component_streams[component_stream_index], begin, end);
	}
}

static void AllocateEntityMapStreams(EntityTypeArray& array, HeapAllocator& heap, u32 old_capacity, u32 new_capacity) {
	array.entity_id_to_stream_index = (u32*)heap.Reallocate(array.entity_id_to_stream_index, old_capacity * sizeof(u32), new_capacity * sizeof(u32), 64u);
	array.stream_index_to_entity_id = (u32*)heap.Reallocate(array.stream_index_to_entity_id, old_capacity * sizeof(u32), new_capacity * sizeof(u32), 64u);
}

CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, EntityTypeID entity_type_id, u32 entity_count) {
	auto& array = system.entity_type_arrays[entity_type_id.index];
	auto& type_info = entity_type_info_table[entity_type_id.index];
	
	if (array.count + entity_count > array.capacity) {
		u32 old_capacity = array.capacity;
		u32 new_capacity = AlignUp(old_capacity ? old_capacity * 3 / 2 + entity_count : entity_count, base_allocation_count);
		
		AllocateEntityMapStreams(array, system.heap, old_capacity, new_capacity);
		AllocateComponentStreams(array.component_streams, type_info.component_type_ids, system.heap, old_capacity, new_capacity);
		
		for (u32 i = old_capacity; i < new_capacity; i += 1) {
			array.stream_index_to_entity_id[i] = i;
		}
		
		array.capacity = new_capacity;
	}
	
	Array<EntityID> entity_ids;
	ArrayResize(entity_ids, alloc, entity_count);
	
	u32 stream_offset = array.count;
	array.count += entity_count;
	
	for (u32 i = 0; i < entity_count; i += 1) {
		u32 entity_id = array.stream_index_to_entity_id[stream_offset + i];
		entity_ids[i] = EntityID{ entity_id };
		
		array.entity_id_to_stream_index[entity_id] = stream_offset + i;
	}
	
	DefaultInitializeStreams(array.component_streams, type_info.component_type_ids, stream_offset, stream_offset + entity_count);
	
	CreateEntitiesResult result;
	result.entity_ids     = entity_ids;
	result.stream_offset  = stream_offset;
	result.entity_type_id = entity_type_id;
	
	auto streams = ExtractComponentStreams<GuidQuery>(&array, stream_offset);
	if (streams.guid) {
		for (u32 i = 0; i < entity_count; i += 1) {
			u64 guid = GenerateRandomNumber64(system.guid_random_seed);
			streams.guid[i].guid = guid;
			
			auto typed_entity_id = TypedEntityID{ entity_ids[i], entity_type_id };
			HashTableAddOrFind(system.entity_guid_to_entity_id, &system.heap, guid, typed_entity_id);
		}
	}
	
	return result;
}

void RemoveEntityByGUID(EntitySystem& system, u64 guid) {
	auto* guid_to_id = HashTableRemove(system.entity_guid_to_entity_id, guid);
	if (guid_to_id == nullptr) return;
	
	u32 entity_id         = guid_to_id->value.entity_id.index;
	u32 entity_type_index = guid_to_id->value.entity_type_id.index;
	
	auto& array  = system.entity_type_arrays[entity_type_index];
	auto& type_info = entity_type_info_table[entity_type_index];
	
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

void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<u8*> output_component_streams, u32 component_stream_offset) {
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
			u64 offset = component_stream_offset * component_type_info_table[match_component_index].size_bytes;
			output_component_streams[stream_indices[match_key_index]] = array->component_streams[array_key_index] + offset;
			array_key_index += 1;
			match_key_index += 1;
		}
	}
}


void InitializeEntitySystem(EntitySystem& system) {
	system.heap = CreateHeapAllocator(2 * 1024 * 1024);
	
	bool random_success = _rdrand64_step(&system.guid_random_seed) != 0;
	DebugAssert(random_success, "Failed to initialize random number generator.");
	
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

void ResetEntitySystem(EntitySystem& system) {
	system.entity_guid_to_entity_id = {};
	system.entity_type_arrays = {};
	system.heap.DeallocateAll();
	
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

// TODO: Simplify this function. There is a lot of code that handles remapping of entity/component
// streams when they change order or are added/removed, but in most cases it's not necessary.
void SaveLoadEntitySystem(SaveLoadBuffer& buffer, EntitySystem& system) {
	if (buffer.is_loading) {
		ResetEntitySystem(system);
	}
	
	buffer.heap = &system.heap;
	
	HashTable<u64, EntityTypeID>    entity_type_hash_to_id;
	HashTable<u64, ComponentTypeID> component_type_hash_to_id;
	if (buffer.is_loading) {
		HashTableReserve(entity_type_hash_to_id, buffer.alloc, entity_type_info_table.count);
		for (u32 entity_type_index = 0; entity_type_index < entity_type_info_table.count; entity_type_index += 1) {
			u64 hash = entity_type_info_table[entity_type_index].type_hash;
			HashTableAddOrFind(entity_type_hash_to_id, hash, EntityTypeID{ entity_type_index });
		}
		
		HashTableReserve(component_type_hash_to_id, buffer.alloc, component_type_info_table.count);
		for (u32 component_type_index = 0; component_type_index < component_type_info_table.count; component_type_index += 1) {
			u64 hash = component_type_info_table[component_type_index].type_hash;
			HashTableAddOrFind(component_type_hash_to_id, hash, ComponentTypeID{ component_type_index });
		}
	}
	
	u64 entity_type_count = entity_type_info_table.count;
	SaveLoad(buffer, entity_type_count);
	
	// Table of entity stream sizes, potentially unaligned.
	u8* entity_stream_sizes = buffer.ReserveSaveLoadBytes(entity_type_count * sizeof(u32));
	
	for (u32 i = 0; i < entity_type_count; i += 1) {
		u32 entity_type_index = u32_max;
		
		if (buffer.is_loading) {
			u64 type_hash = 0;
			SaveLoad(buffer, type_hash);
			
			auto* element = HashTableFind(entity_type_hash_to_id, type_hash);
			entity_type_index = element ? element->value.index : u32_max;
			
			if (entity_type_index == u32_max) {
				u32 stream_size = 0;
				memcpy(&stream_size, entity_stream_sizes + i * sizeof(u32), sizeof(u32));
				
				buffer.ReserveLoadBytes(stream_size);
				continue;
			}
		} else if (buffer.is_saving) {
			u64 type_hash = entity_type_info_table[i].type_hash;
			SaveLoad(buffer, type_hash);
			
			entity_type_index = i;
		}
		
		u32 begin_entity_offset = buffer.global_offset;
		
		auto& array = system.entity_type_arrays[entity_type_index];
		auto& entity_type_info = entity_type_info_table[entity_type_index];
		auto component_type_ids = entity_type_info.component_type_ids;
		
		u32 count = array.count;
		SaveLoad(buffer, count);
		
		if (buffer.is_loading) {
			u32 new_capacity = AlignUp(count, base_allocation_count);
			AllocateEntityMapStreams(array, system.heap, 0, new_capacity);
			AllocateComponentStreams(array.component_streams, component_type_ids, system.heap, 0, new_capacity);
			DefaultInitializeStreams(array.component_streams, component_type_ids, 0, count); // TODO: Only default initialize new component streams.
			
			array.capacity = new_capacity;
			array.count    = count;
		}
		
		u64 component_stream_count = component_type_ids.count;
		SaveLoad(buffer, component_stream_count);
		
		// Table of component stream sizes, potentially unaligned.
		u8* component_stream_sizes = buffer.ReserveSaveLoadBytes(component_stream_count * sizeof(u32));
		
		for (u32 i = 0; i < component_stream_count; i += 1) {
			auto component_type_id = ComponentTypeID{ u32_max };
			u32 component_stream_index = u32_max;
			
			if (buffer.is_loading) {
				u64 type_hash = 0;
				SaveLoad(buffer, type_hash);
				
				auto* element = HashTableFind(component_type_hash_to_id, type_hash);
				component_type_id = element ? element->value : ComponentTypeID{ u32_max };
				for (u32 i = 0; i < component_type_ids.count; i += 1) {
					if (component_type_ids[i].index == component_type_id.index) {
						component_stream_index = i;
						break;
					}
				}
				
				if (component_stream_index == u32_max) {
					u32 stream_size = 0;
					memcpy(&stream_size, component_stream_sizes + i * sizeof(u32), sizeof(u32));
					
					buffer.ReserveLoadBytes(stream_size);
					continue;
				}
			} else if (buffer.is_saving) {
				component_stream_index = i;
				component_type_id = component_type_ids[component_stream_index];
				
				u64 type_hash = component_type_info_table[component_type_id.index].type_hash;
				SaveLoad(buffer, type_hash);
			}
			
			u32 begin_component_offset = buffer.global_offset;
			
			auto save_load_callback = component_save_load_callbacks[component_type_id.index];
			auto type_info          = component_type_info_table[component_type_id.index];
			u8*  component_stream   = array.component_streams[component_stream_index];
			
			u64 version = type_info.version;
			SaveLoad(buffer, version);
			
			for (u32 i = 0; i < array.count; i += 1) {
				save_load_callback(buffer, component_stream + i * type_info.size_bytes, version);
			}
			u32 end_component_offset = buffer.global_offset;
			
			u32 component_stream_size = end_component_offset - begin_component_offset;
			
			if (buffer.is_loading) {
				u32 stream_size = 0;
				memcpy(&stream_size, component_stream_sizes + i * sizeof(u32), sizeof(u32));
				
				DebugAssert(component_stream_size == stream_size, "Incorrect number of bytes loaded from a component stream. (%/%).", component_stream_size, stream_size);
			} else if (buffer.is_saving) {
				memcpy(component_stream_sizes + component_stream_index * sizeof(u32), &component_stream_size, sizeof(u32));
			}
		}
		
		if (buffer.is_loading) {
			for (u32 i = 0; i < array.capacity; i += 1) {
				array.stream_index_to_entity_id[i] = i;
			}
		}
		
		u32 end_entity_offset = buffer.global_offset;
		
		u32 entity_stream_size = end_entity_offset - begin_entity_offset;
		
		if (buffer.is_loading) {
			u32 stream_size = 0;
			memcpy(&stream_size, entity_stream_sizes + i * sizeof(u32), sizeof(u32));
			
			DebugAssert(entity_stream_size == stream_size, "Incorrect number of bytes loaded from an entity stream. (%/%).", entity_stream_size, stream_size);
		} else if (buffer.is_saving) {
			memcpy(entity_stream_sizes + entity_type_index * sizeof(u32), &entity_stream_size, sizeof(u32));
		}
	}
	
	if (buffer.is_loading) {
		auto guid_query = QueryEntities<GuidQuery>(buffer.alloc, system);
		
		u32 count = 0;
		for (auto* entity_array : guid_query) {
			count += entity_array->count;
		}
		
		HashTableReserve(system.entity_guid_to_entity_id, &system.heap, count);
		
		for (auto* entity_array : guid_query) {
			auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
			
			auto entity_type_id = entity_array->entity_type_id;
			auto* entity_ids = entity_array->stream_index_to_entity_id;
			for (u32 index = 0; index < entity_array->count; index += 1) {
				auto& [guid] = streams.guid[index];
				auto entity_id = EntityID{ entity_ids[index] };
				
				HashTableAddOrFind(system.entity_guid_to_entity_id, guid, TypedEntityID{ entity_id, entity_type_id });
			}
		}
	}
}
