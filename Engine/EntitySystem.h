#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMath.h"

compile_const u32 component_type_count = 5;

struct alignas(u64) EntityTypeKey {
	compile_const u32 key_size_dwords = DivideAndRoundUp(component_type_count, 8u);
	compile_const u32 key_size_bytes  = key_size_dwords * 4;
	
	FixedCountArray<u32, key_size_dwords> active_component_mask;
	
	bool operator== (const EntityTypeKey& other) const { return memcmp(active_component_mask.data, other.active_component_mask.data, sizeof(active_component_mask)) == 0; }
};

struct ComponentTypeInfo {
	u32 size_bytes  = 0;
	u32 align_bytes = 0;
};

struct EntityTypeArray {
	u32 count    = 0;
	u32 capacity = 0;
	
	u32* entity_id_to_stream_index = nullptr;
	u32* stream_index_to_entity_id = nullptr;
	
	FixedCapacityArray<u8*, component_type_count> streams;
	u32 entity_type_index = 0;
	
	EntityTypeKey key;
};

struct EntitySystem {
	HashTable<u64, u32> entity_guid_to_entity_id;
	
	HashTable<EntityTypeKey, u32> entity_type_key_to_entity_type_index;
	Array<EntityTypeArray> entity_type_arrays;
	
	HeapAllocator heap;
};

struct CreateEntitiesResult {
	Array<u32> entity_ids;
	u32 stream_offset = 0;
	u32 entity_type_index = 0;
};

CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, const EntityTypeKey& key, u32 entity_count);
void RemoveEntity(EntitySystem& system, u32 entity_type_index, u32 entity_id);

ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system, const EntityTypeKey& key);
void ExtractComponentStreams(EntityTypeArray* array, const EntityTypeKey& output_key, ArrayView<u8*> output_streams);

template<typename ... Ts>
inline EntityTypeKey CreateEntityTypeKey() {
	u64 key_u64 = 0;
	((key_u64 |= (1ull << Ts::component_type_id)), ...);
	
	EntityTypeKey result;
	memcpy(result.active_component_mask.data, &key_u64, result.key_size_bytes);
	static_assert(result.key_size_bytes <= sizeof(key_u64));
	
	return result;
}

template<typename ... Ts>
CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, u32 entity_count) {
	return CreateEntities(alloc, system, CreateEntityTypeKey<Ts ...>(), entity_count);
}

template<typename T>
ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system) {
	return QueryEntities(alloc, system, T::key);
}

template<typename T>
T ExtractComponentStreams(EntityTypeArray* array) {
	FixedCountArray<u8*, sizeof(T) / sizeof(u8*)> streams;
	static_assert(sizeof(streams) == sizeof(T));
	
	ExtractComponentStreams(array, T::key, streams);
	
	T result;
	memcpy(&result, streams.data, sizeof(result));
	
	return result;
}
