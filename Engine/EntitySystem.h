#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicString.h"

struct EntityTypeID      { u32 index = 0; };
struct EntityQueryTypeID { u32 index = 0; };
struct ComponentTypeID   { u32 index = 0; };

namespace ESC {
	NOTES()
	enum struct ComponentType : u32 {
		CPU = 0,
		GPU = 1,
	};
	
	NOTES(ESC::ComponentType::CPU) template<typename T> struct Component {};
	NOTES(ESC::ComponentType::GPU) template<typename T> struct GpuComponent {};
	
	template<typename T> struct GetEntityTypeID      { static EntityTypeID      id; };
	template<typename T> struct GetEntityQueryTypeID { static EntityQueryTypeID id; };
	template<typename T> struct GetComponentTypeID   { static ComponentTypeID   id; };
}

namespace Meta {
	NOTES() struct EntityType {};
	NOTES() struct ComponentQuery {};
}

struct EntityTypeInfo {
	ArrayView<ComponentTypeID> component_type_ids;
};

struct EntityQueryTypeInfo {
	ArrayView<ComponentTypeID> component_type_ids;
	ArrayView<u32>       component_stream_indices;
};

struct ComponentTypeInfo {
	u32 size_bytes = 0;
};

struct EntityTypeArray {
	u32 count    = 0;
	u32 capacity = 0;
	
	u32* entity_id_to_stream_index = nullptr;
	u32* stream_index_to_entity_id = nullptr;
	
	ArrayView<u8*> component_streams;
	EntityTypeID entity_type_id = { 0 };
};

struct EntitySystem {
	HashTable<u64, u32>  entity_guid_to_entity_id;
	ArrayView<EntityTypeArray> entity_type_arrays;
	
	HeapAllocator heap;
};

struct CreateEntitiesResult {
	Array<u32> entity_ids;
	u32 stream_offset = 0;
	EntityTypeID entity_type_id = { 0 };
};

void InitializeEntitySystem(EntitySystem& system);

CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, EntityTypeID entity_type_id, u32 entity_count);
void RemoveEntity(EntitySystem& system, EntityTypeID entity_type_id, u32 entity_id);

ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system, EntityQueryTypeID query_type_id);
void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<u8*> output_streams);


template<typename EntityTypeT>
CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, u32 entity_count) {
	return CreateEntities(alloc, system, ESC::GetEntityTypeID<EntityTypeT>::id, entity_count);
}

template<typename QueryTypeT>
ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system) {
	return QueryEntities(alloc, system, ESC::GetEntityQueryTypeID<QueryTypeT>::id);
}

template<typename QueryTypeT>
QueryTypeT ExtractComponentStreams(EntityTypeArray* array) {
	FixedCountArray<u8*, sizeof(QueryTypeT) / sizeof(u8*)> component_streams;
	static_assert(sizeof(component_streams) == sizeof(QueryTypeT));
	
	ExtractComponentStreams(array, ESC::GetEntityQueryTypeID<QueryTypeT>::id, component_streams);
	
	QueryTypeT result;
	memcpy(&result, component_streams.data, sizeof(result));
	
	return result;
}

