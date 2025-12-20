#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicString.h"

struct SaveLoadBuffer;

struct EntityTypeID      { u32 index = 0; };
struct EntityQueryTypeID { u32 index = 0; };
struct ComponentTypeID   { u32 index = 0; };
struct EntityID          { u32 index = 0; };
struct TypedEntityID     { EntityID entity_id; EntityTypeID entity_type_id; };

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
	EntityTypeID entity_type_id;
};

struct EntitySystem {
	HashTable<u64, TypedEntityID> entity_guid_to_entity_id;
	ArrayView<EntityTypeArray> entity_type_arrays;
	u64 guid_random_seed = 0x7C7C4065B00D53D3ull;
	
	HeapAllocator heap;
};

struct CreateEntitiesResult {
	Array<EntityID> entity_ids;
	u32 stream_offset = 0;
	EntityTypeID entity_type_id;
};

void InitializeEntitySystem(EntitySystem& system);
void SaveLoadEntitySystem(SaveLoadBuffer& buffer, EntitySystem& system);

CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, EntityTypeID entity_type_id, u32 entity_count);
void RemoveEntityByGUID(EntitySystem& system, u64 guid);

ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system, EntityQueryTypeID query_type_id);
void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<u8*> output_streams, u32 component_stream_offset = 0);


template<typename EntityTypeT>
CreateEntitiesResult CreateEntities(StackAllocator* alloc, EntitySystem& system, u32 entity_count) {
	return CreateEntities(alloc, system, ESC::GetEntityTypeID<EntityTypeT>::id, entity_count);
}

template<typename QueryTypeT>
ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system) {
	return QueryEntities(alloc, system, ESC::GetEntityQueryTypeID<QueryTypeT>::id);
}

template<typename EntityTypeT>
EntityTypeArray* QueryEntityTypeArray(EntitySystem& system) {
	return &system.entity_type_arrays[ESC::GetEntityTypeID<EntityTypeT>::id.index];
}

template<typename QueryTypeT>
QueryTypeT ExtractComponentStreams(EntityTypeArray* array, u32 component_stream_offset = 0) {
	FixedCountArray<u8*, sizeof(QueryTypeT) / sizeof(u8*)> component_streams;
	static_assert(sizeof(component_streams) == sizeof(QueryTypeT));
	
	ExtractComponentStreams(array, ESC::GetEntityQueryTypeID<QueryTypeT>::id, component_streams, component_stream_offset);
	
	QueryTypeT result;
	memcpy(&result, component_streams.data, sizeof(result));
	
	return result;
}

template<typename QueryTypeT>
QueryTypeT QueryEntityByGUID(EntitySystem& system, u64 entity_guid) {
	auto* element = HashTableFind(system.entity_guid_to_entity_id, entity_guid);
	DebugAssert(element, "Failed to find entity by guid 0x%llX.", entity_guid);
	
	auto typed_entity_id = element->value;
	
	auto& array = system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	return ExtractComponentStreams<QueryTypeT>(&array, array.entity_id_to_stream_index[typed_entity_id.entity_id.index]);
}


NOTES()
struct GuidComponent {
	u64 guid = 0;
};

NOTES(Meta::ComponentQuery{})
struct GuidQuery {
	GuidComponent* guid = nullptr;
};
