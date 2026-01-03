#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMemory.h"

struct SaveLoadBuffer;
enum struct VirtualResourceID : u32;

struct EntityTypeID      { u32 index = 0; };
struct EntityQueryTypeID { u32 index = 0; };
struct ComponentTypeID   { u32 index = 0; };
struct EntityID          { u32 index = 0; };
struct TypedEntityID     { EntityID entity_id; EntityTypeID entity_type_id; };

compile_const u32 entity_system_base_allocation_count = 1024;

NOTES()
enum struct ComponentType : u32 {
	CPU = 0,
	GPU = 1,
};

namespace ECS {
	NOTES(ComponentType::CPU)
	template<typename T>
	struct alignas(void*) Component {
		T* data = nullptr;
		T& operator[] (u64 index) { return data[index]; }
		T* operator-> () { return data; }
		T& operator* () { return *data; }
	};
	
	NOTES(ComponentType::GPU)
	template<typename T>
	struct alignas(void*) GpuComponent {
		u32 offset = 0;
		VirtualResourceID resource_id = (VirtualResourceID)0;
	};
	
	template<typename T> struct GetEntityTypeID      { static EntityTypeID      id; };
	template<typename T> struct GetEntityQueryTypeID { static EntityQueryTypeID id; };
	template<typename T> struct GetComponentTypeID   { static ComponentTypeID   id; };
}

union alignas(void*) ComponentStream {
	u64 handle = 0;
	ECS::Component<u8> cpu;
	ECS::GpuComponent<u8> gpu;
};

namespace Meta {
	NOTES() struct NoSaveLoad {};
	NOTES() struct EntityType { u32 base_allocation_count = entity_system_base_allocation_count; };
	NOTES() struct ComponentQuery {};
}

struct EntityTypeInfo {
	ArrayView<ComponentTypeID> component_type_ids;
	ArrayView<u32>           virtual_resource_ids;
	u32 cpu_component_count = 0;
	u32 gpu_component_count = 0;
	
	u32 base_allocation_count = 0;
	u64 type_hash = 0;
};

struct EntityQueryTypeInfo {
	ArrayView<ComponentTypeID> component_type_ids;
	ArrayView<u32>       component_stream_indices;
};

struct ComponentTypeInfo {
	u64 size_bytes = 0;
	u64 version    = 0;
	u64 type_hash  = 0;
	ComponentType component_type = ComponentType::CPU;
};

using DefaultInitializeCallback = void (*)(void* data, u64 begin, u64 end);

struct EntityTypeArray {
	u32 count    = 0;
	u32 capacity = 0;
	
	u32* entity_id_to_stream_index = nullptr;
	u32* stream_index_to_entity_id = nullptr;
	
	ArrayView<u64> dirty_mask;
	
	ArrayView<ComponentStream> component_streams;
	EntityTypeID entity_type_id;
};

struct EntitySystem {
	HashTable<u64, TypedEntityID> entity_guid_to_entity_id;
	ArrayView<EntityTypeArray> entity_type_arrays;
	u64 guid_random_seed = 0x7C7C4065B00D53D3ull;
	
	HeapAllocator heap;
};

void InitializeEntitySystem(EntitySystem& system);
void SaveLoadEntitySystem(SaveLoadBuffer& buffer, EntitySystem& system);
void ClearEntityDirtyMasks(EntitySystem& system);

u32 CreateEntities(EntitySystem& system, EntityTypeID entity_type_id, u32 entity_count);
void RemoveEntityByGUID(EntitySystem& system, u64 guid);

ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system, EntityQueryTypeID query_type_id);
void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<ComponentStream> output_streams, u32 component_stream_offset = 0);

template<typename QueryTypeT>
QueryTypeT ExtractComponentStreams(EntityTypeArray* array, u32 component_stream_offset = 0) {
	FixedCountArray<ComponentStream, sizeof(QueryTypeT) / sizeof(ComponentStream)> component_streams;
	static_assert(sizeof(component_streams) == sizeof(QueryTypeT));
	
	ExtractComponentStreams(array, ECS::GetEntityQueryTypeID<QueryTypeT>::id, component_streams, component_stream_offset);
	
	QueryTypeT result;
	memcpy(&result, component_streams.data, sizeof(result));
	
	return result;
}

template<typename EntityTypeT>
EntityTypeArray* QueryEntityTypeArray(EntitySystem& system) {
	return &system.entity_type_arrays[ECS::GetEntityTypeID<EntityTypeT>::id.index];
}

template<typename EntityTypeT>
EntityTypeT CreateEntities(EntitySystem& system, u32 entity_count) {
	u32 stream_offset = CreateEntities(system, ECS::GetEntityTypeID<EntityTypeT>::id, entity_count);
	return ExtractComponentStreams<EntityTypeT>(QueryEntityTypeArray<EntityTypeT>(system), stream_offset);
}

template<typename QueryTypeT>
ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystem& system) {
	return QueryEntities(alloc, system, ECS::GetEntityQueryTypeID<QueryTypeT>::id);
}

template<typename QueryTypeT>
QueryTypeT QueryEntityByGUID(EntitySystem& system, u64 entity_guid) {
	auto* element = HashTableFind(system.entity_guid_to_entity_id, entity_guid);
	DebugAssert(element, "Failed to find entity by guid 0x%x.", entity_guid);
	
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
