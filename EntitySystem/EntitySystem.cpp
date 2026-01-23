#include "EntitySystem.h"
#include "Basic/BasicSaveLoad.h"
#include "Basic/BasicBitArray.h"
#include "Components.h"

extern ArrayView<EntityTypeInfo>      entity_type_info_table;
extern ArrayView<EntityQueryTypeInfo> entity_query_type_info_table;
extern ArrayView<ComponentTypeInfo>   component_type_info_table;
extern ArrayView<SaveLoadCallback>    component_save_load_callbacks;
extern ArrayView<DefaultInitializeCallback> component_default_initialize_callbacks;

template<typename Lambda>
static void IterateComponentStreams(ArrayView<ComponentStream> component_streams, EntityTypeInfo& type_info, Lambda&& lambda) {
	for (u32 component_stream_index = 0; component_stream_index < type_info.cpu_component_count; component_stream_index += 1) {
		lambda(component_streams[component_stream_index].cpu.data, type_info.component_type_ids[component_stream_index]);
	}
}

static void AllocateComponentStreams(ArrayView<ComponentStream> component_streams, EntityTypeInfo& type_info, HeapAllocator& heap, u32 old_capacity, u32 new_capacity) {
	IterateComponentStreams(component_streams, type_info, [&](u8*& stream, ComponentTypeID component_type_id) {
		auto type_info = component_type_info_table[component_type_id.index];
		stream = (u8*)heap.Reallocate(stream, old_capacity * type_info.size_bytes, new_capacity * type_info.size_bytes, 64u);
	});
}

static void DefaultInitializeStreams(ArrayView<ComponentStream> component_streams, EntityTypeInfo& type_info, u64 offset, u64 count) {
	IterateComponentStreams(component_streams, type_info, [&](u8* stream, ComponentTypeID component_type_id) {
		auto default_initialize = component_default_initialize_callbacks[component_type_id.index];
		default_initialize(stream, offset, offset + count);
	});
}

static void AllocateEntityMaskStream(HeapAllocator& heap, ArrayView<u64>& entity_mask, u32 new_mask_count) {
	u64 old_mask_count = entity_mask.count;
	
	entity_mask.data  = (u64*)heap.Reallocate(entity_mask.data, old_mask_count * sizeof(u64), new_mask_count * sizeof(u64), 64u);
	entity_mask.count = new_mask_count;
	
	memset(entity_mask.data + old_mask_count, 0, (new_mask_count - old_mask_count) * sizeof(u64));
}

static void AllocateEntityMapStreams(EntityTypeArray& array, HeapAllocator& heap, u32 old_capacity, u32 new_capacity) {
	u32 new_mask_count = DivideAndRoundUp(new_capacity, 64u);
	AllocateEntityMaskStream(heap, array.created_mask, new_mask_count);
	AllocateEntityMaskStream(heap, array.removed_mask, new_mask_count);
	AllocateEntityMaskStream(heap, array.alive_mask,   new_mask_count);
	AllocateEntityMaskStream(heap, array.dirty_mask,   new_mask_count);
}

EntityID CreateEntity(EntitySystemBase& system, EntityTypeID entity_type_id, u64 optional_guid) {
	ProfilerScope("CreateEntity");
	
	auto& array = system.entity_type_arrays[entity_type_id.index];
	auto& type_info = entity_type_info_table[entity_type_id.index];
	
	if (array.entity_id_free_list.count == 0) {
		ProfilerScope("ReallocateComponentStreams");
		
		u32 old_capacity = array.capacity;
		u32 new_capacity = AlignUp(old_capacity ? old_capacity * 3 / 2 : 1, type_info.base_allocation_count);
		
		AllocateEntityMapStreams(array, system.heap, old_capacity, new_capacity);
		AllocateComponentStreams(array.component_streams, type_info, system.heap, old_capacity, new_capacity);
		
		for (u32 i = old_capacity; i < new_capacity; i += 1) {
			ArrayAppend(array.entity_id_free_list, &system.heap, EntityID{ i });
		}
		
		array.capacity = new_capacity;
	}
	
	auto entity_id = ArrayPopLast(array.entity_id_free_list);
	BitArraySetBit(array.created_mask, entity_id.index);
	BitArraySetBit(array.alive_mask,   entity_id.index);
	BitArraySetBit(array.dirty_mask,   entity_id.index);
	
	DefaultInitializeStreams(array.component_streams, type_info, entity_id.index, 1);
	
	auto streams = ExtractComponentStreams<GuidQuery>(&array);
	if (streams.guid) {
		u64 guid = optional_guid != 0 ? optional_guid : GenerateRandomNumber64(system.guid_random_seed);
		streams.guid[entity_id.index].guid = guid;
		
		HashTableAddOrFind(system.entity_guid_to_entity_id, &system.heap, guid, { entity_id, entity_type_id });
	}
	
	array.count += 1;
	
	return entity_id;
}

void RemoveEntityByGUID(EntitySystemBase& system, u64 guid) {
	auto* guid_to_id = HashTableRemove(system.entity_guid_to_entity_id, guid);
	if (guid_to_id == nullptr) return;
	
	auto& array = system.entity_type_arrays[guid_to_id->value.entity_type_id.index];
	array.count -= 1;
	
	auto entity_id = guid_to_id->value.entity_id;
	BitArraySetBit(array.removed_mask, entity_id.index);
	BitArrayResetBit(array.alive_mask, entity_id.index);
	BitArraySetBit(array.dirty_mask,   entity_id.index);
}


ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystemBase& system, EntityQueryTypeID query_type_id) {
	ProfilerScope("QueryEntities");
	
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
			ArrayAppend(result, alloc, &system.entity_type_arrays[entity_type_index]);
		}
	}
	
	return result;
}

void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<ComponentStream> output_component_streams, EntityID base_entity_id) {
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
			match_key_index += 1;
		} else {
			u64 offset = base_entity_id.index * component_type_info_table[match_component_index].size_bytes;
			output_component_streams[stream_indices[match_key_index]].handle = array->component_streams[array_key_index].handle + offset;
			array_key_index += 1;
			match_key_index += 1;
		}
	}
}


static void CreateEntityTypeArrays(EntitySystemBase& system) {
	Array<EntityTypeArray> entity_type_arrays;
	ArrayResize(entity_type_arrays, &system.heap, entity_type_info_table.count);
	
	for (u32 entity_type_index = 0; entity_type_index < entity_type_info_table.count; entity_type_index += 1) {
		auto& array     = entity_type_arrays[entity_type_index];
		auto& type_info = entity_type_info_table[entity_type_index];
		
		Array<ComponentStream> component_streams;
		ArrayResize(component_streams, &system.heap, type_info.component_type_ids.count);
		
		for (u32 i = 0; i < type_info.virtual_resource_ids.count; i += 1) {
			u32 resource_id = type_info.virtual_resource_ids[i];
			if (resource_id != 0) {
				component_streams[i].gpu = { 0, (VirtualResourceID)resource_id };
			} else {
				component_streams[i].cpu = { nullptr };
			}
		}
		
		array.component_streams    = component_streams;
		array.entity_type_id.index = entity_type_index;
	}
	
	system.entity_type_arrays = entity_type_arrays;
}

void InitializeEntitySystem(EntitySystemBase& system) {
	system.heap = CreateHeapAllocator(2 * 1024 * 1024);
	
	bool random_success = _rdrand64_step(&system.guid_random_seed) != 0;
	DebugAssert(random_success, "Failed to initialize random number generator.");
	
	CreateEntityTypeArrays(system);
}

static void ResetEntitySystem(EntitySystemBase& system) {
	system.entity_guid_to_entity_id = {};
	system.entity_type_arrays = {};
	system.heap.DeallocateAll();
	
	CreateEntityTypeArrays(system);
}

void ClearEntityMasks(EntitySystemBase& system) {
	for (auto& array : system.entity_type_arrays) {
		for (u64 entity_index : BitArrayIt(array.removed_mask)) {
			ArrayAppend(array.entity_id_free_list, &system.heap, EntityID{ (u32)entity_index });
		}
	}
	
	for (auto& array : system.entity_type_arrays) {
		memset(array.created_mask.data, 0, array.created_mask.count * sizeof(u64));
		memset(array.removed_mask.data, 0, array.removed_mask.count * sizeof(u64));
		memset(array.dirty_mask.data,   0, array.dirty_mask.count   * sizeof(u64));
	}
}

// TODO: Simplify this function. There is a lot of code that handles remapping of entity/component
// streams when they change order or are added/removed, but in most cases it's not necessary.
void SaveLoadEntitySystem(SaveLoadBuffer& buffer, EntitySystemBase& system) {
	ProfilerScope("SaveLoadEntitySystem");
	
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
		
		u64 begin_entity_offset = buffer.data.count;
		
		auto& array = system.entity_type_arrays[entity_type_index];
		auto& entity_type_info = entity_type_info_table[entity_type_index];
		auto component_type_ids = entity_type_info.component_type_ids;
		
		u32 count = array.count;
		SaveLoad(buffer, count);
		
		if (buffer.is_loading) {
			u32 new_capacity = AlignUp(count, entity_type_info.base_allocation_count);
			AllocateEntityMapStreams(array, system.heap, 0, new_capacity);
			AllocateComponentStreams(array.component_streams, entity_type_info, system.heap, 0, new_capacity);
			DefaultInitializeStreams(array.component_streams, entity_type_info, 0, count); // TODO: Only default initialize new component streams.
			
			array.capacity = new_capacity;
			array.count    = count;
		}
		
		u64 component_stream_count = entity_type_info.cpu_component_count;
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
			
			u64 begin_component_offset = buffer.data.count;
			
			auto type_info = component_type_info_table[component_type_id.index];
			
			u64 version = type_info.version;
			SaveLoad(buffer, version);
			
			auto save_load_callback = component_save_load_callbacks[component_type_id.index];
			if (save_load_callback != nullptr) {
				u8* component_stream = array.component_streams[component_stream_index].cpu.data;
				if (buffer.is_loading) {
					for (u32 i = 0; i < array.count; i += 1) {
						save_load_callback(buffer, component_stream + i * type_info.size_bytes, version);
					}
				} else if (buffer.is_saving) {
					for (u64 i : BitArrayIt(array.alive_mask)) {
						save_load_callback(buffer, component_stream + i * type_info.size_bytes, version);
					}
				}
			}
			u64 end_component_offset = buffer.data.count;
			
			u32 component_stream_size = (u32)(end_component_offset - begin_component_offset);
			
			if (buffer.is_loading) {
				u32 stream_size = 0;
				memcpy(&stream_size, component_stream_sizes + i * sizeof(u32), sizeof(u32));
				
				DebugAssert(component_stream_size == stream_size, "Incorrect number of bytes loaded from a component stream. (%/%).", component_stream_size, stream_size);
			} else if (buffer.is_saving) {
				memcpy(component_stream_sizes + component_stream_index * sizeof(u32), &component_stream_size, sizeof(u32));
			}
		}
		
		if (buffer.is_loading) {
			for (u32 i = array.count; i < array.capacity; i += 1) {
				ArrayAppend(array.entity_id_free_list, &system.heap, EntityID{ i });
			}
			BitArraySetBitRange(array.created_mask, 0, array.count);
			BitArraySetBitRange(array.alive_mask,   0, array.count);
			BitArraySetBitRange(array.dirty_mask,   0, array.count);
		}
		
		u64 end_entity_offset = buffer.data.count;
		
		u32 entity_stream_size = (u32)(end_entity_offset - begin_entity_offset);
		
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
			for (u32 entity_index = 0; entity_index < entity_array->count; entity_index += 1) {
				auto& [guid] = streams.guid[entity_index];
				auto entity_id = EntityID{ entity_index };
				
				HashTableAddOrFind(system.entity_guid_to_entity_id, guid, TypedEntityID{ entity_id, entity_type_id });
			}
		}
	}
}

void SaveLoadEntityForTooling(SaveLoadBuffer& buffer, EntityTypeArray* array, EntityID entity_id) {
	auto entity_type_id = array->entity_type_id;
	
	auto& entity_type_info = entity_type_info_table[entity_type_id.index];
	auto component_type_ids = entity_type_info.component_type_ids;
	
	u64 component_stream_count = entity_type_info.cpu_component_count;
	
	for (u32 i = 0; i < component_stream_count; i += 1) {
		auto component_type_id = component_type_ids[i];
		auto type_info = component_type_info_table[component_type_id.index];
		
		if (component_type_id.index == ECS::GetComponentTypeID<GuidComponent>::id.index) continue;
		
		auto save_load_callback = component_save_load_callbacks[component_type_id.index];
		if (save_load_callback != nullptr) {
			u8* component_stream = array->component_streams[i].cpu.data;
			save_load_callback(buffer, component_stream + entity_id.index * type_info.size_bytes, type_info.version);
		}
	}
}
