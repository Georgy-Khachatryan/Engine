#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicString.h"

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
	GpuMask = 2,
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
	
	NOTES(ComponentType::GpuMask)
	template<typename T>
	struct GpuMaskComponent : GpuComponent<T> {};
	
	template<typename T> struct GetEntityTypeID      { static EntityTypeID      id; };
	template<typename T> struct GetEntityQueryTypeID { static EntityQueryTypeID id; };
	template<typename T> struct GetComponentTypeID   { static ComponentTypeID   id; };
}

union alignas(void*) ComponentStream {
	u64 handle = 0;
	ECS::Component<u8> cpu;
	ECS::GpuComponent<u8> gpu;
};

struct GpuComponentStreamAllocation {
	void* handle = nullptr;
	u32 size = 0;
	
	VirtualResourceID resource_id = (VirtualResourceID)0;
	EntityTypeID entity_type_id;
	ComponentTypeID component_type_id;
};

NOTES()
enum struct SaveLoadFlags : u32 {
	None = 0,
	
	SaveLoadToDisk      = 1u << 0,
	SaveLoadForTooling  = 1u << 1, // SaveLoad for Undo/Redo and entity duplication.
	SaveLoadOptionsMask = SaveLoadToDisk | SaveLoadForTooling,
	
	CustomSaveLoadCallback = 1u << 2, // Use a manually implemented SaveLoad(...) function instead of generating one.
	
	Default = SaveLoadToDisk | SaveLoadForTooling,
};
ENUM_FLAGS_OPERATORS(SaveLoadFlags);

namespace Meta {
	NOTES() struct SaveLoadOptions { SaveLoadFlags flags = SaveLoadFlags::Default; };
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
	ComponentType component_type  = ComponentType::CPU;
	SaveLoadFlags save_load_flags = SaveLoadFlags::None;
};

using DefaultInitializeCallback = void (*)(void* data, u64 begin, u64 end);

struct EntityTypeArray {
	u32 count    = 0;
	u32 capacity = 0;
	
	Array<EntityID> entity_id_free_list;
	
	ArrayView<u64> created_mask; // Created this frame.
	ArrayView<u64> removed_mask; // Removed this frame.
	ArrayView<u64> dirty_mask; // Anything changed on the entity.
	ArrayView<u64> alive_mask; // Any alive entity.
	ArrayView<u64> prev_dirty_mask; // Anything changed on the entity last frame.
	
	ArrayView<ComponentStream> component_streams;
	EntityTypeID entity_type_id;
};

struct EntitySystemBase {
	HashTable<u64, TypedEntityID> entity_guid_to_entity_id;
	ArrayView<EntityTypeArray> entity_type_arrays;
	u64 guid_random_seed = 0x7C7C4065B00D53D3ull;
	
	ArrayView<GpuComponentStreamAllocation> gpu_component_stream_allocations;
	bool clear_gpu_mask_component_streams = false;
	
	HeapAllocator heap;
};

struct WorldEntitySystem : EntitySystemBase {};
struct AssetEntitySystem : EntitySystemBase {};


void InitializeEntitySystem(EntitySystemBase& system, StackAllocator* alloc);
void SaveLoadEntitySystem(SaveLoadBuffer& buffer, EntitySystemBase& system);
bool SaveLoadEntitySystemToFile(StackAllocator* alloc, EntitySystemBase& system, String filepath, SaveLoadDirection direction);
void ResetEntitySystem(EntitySystemBase& system);
void ClearEntityMasks(EntitySystemBase& system);
void SaveLoadEntityForTooling(SaveLoadBuffer& buffer, EntityTypeArray* array, EntityID entity_id);

EntityID CreateEntity(EntitySystemBase& system, EntityTypeID entity_type_id, u64 optional_guid = 0);
void RemoveEntityByGUID(EntitySystemBase& system, u64 guid);

ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystemBase& system, EntityQueryTypeID query_type_id);
void ExtractComponentStreams(EntityTypeArray* array, EntityQueryTypeID query_type_id, ArrayView<ComponentStream> output_streams, EntityID base_entity_id = { 0 });

template<typename QueryTypeT>
QueryTypeT ExtractComponentStreams(EntityTypeArray* array, EntityID base_entity_id = { 0 }) {
	FixedCountArray<ComponentStream, sizeof(QueryTypeT) / sizeof(ComponentStream)> component_streams;
	static_assert(sizeof(component_streams) == sizeof(QueryTypeT));
	
	ExtractComponentStreams(array, ECS::GetEntityQueryTypeID<QueryTypeT>::id, component_streams, base_entity_id);
	
	QueryTypeT result;
	memcpy(&result, component_streams.data, sizeof(result));
	
	return result;
}

template<typename EntityTypeT>
EntityTypeArray* QueryEntityTypeArray(EntitySystemBase& system) {
	return &system.entity_type_arrays[ECS::GetEntityTypeID<EntityTypeT>::id.index];
}

template<typename EntityTypeT>
EntityTypeT CreateEntity(EntitySystemBase& system, u64 optional_guid = 0) {
	auto entity_id = CreateEntity(system, ECS::GetEntityTypeID<EntityTypeT>::id, optional_guid);
	return ExtractComponentStreams<EntityTypeT>(QueryEntityTypeArray<EntityTypeT>(system), entity_id);
}

template<typename QueryTypeT>
ArrayView<EntityTypeArray*> QueryEntities(StackAllocator* alloc, EntitySystemBase& system) {
	return QueryEntities(alloc, system, ECS::GetEntityQueryTypeID<QueryTypeT>::id);
}

inline TypedEntityID FindEntityByGUID(EntitySystemBase& system, u64 guid) {
	auto* element = HashTableFind(system.entity_guid_to_entity_id, guid);
	DebugAssert(element, "Failed to find entity by GUID 0x%x.", guid);
	
	return element->value;
}

template<typename QueryTypeT>
QueryTypeT QueryEntityByGUID(EntitySystemBase& system, u64 guid) {
	auto typed_entity_id = FindEntityByGUID(system, guid);
	auto& array = system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	return ExtractComponentStreams<QueryTypeT>(&array, typed_entity_id.entity_id);
}

inline TypedEntityID FindFirstEntityByType(EntitySystemBase& system, EntityTypeID entity_type_id) {
	auto& array = system.entity_type_arrays[entity_type_id.index];
	DebugAssert(array.count != 0, "Failed to find first entity.");
	return TypedEntityID{ EntityID{ (u32)BitArrayFindFirstSetBit(array.alive_mask) }, entity_type_id };
}

template<typename EntityTypeT>
inline EntityTypeT QueryFirstEntityByType(EntitySystemBase& system) {
	auto typed_entity_id = FindFirstEntityByType(system, ECS::GetEntityTypeID<EntityTypeT>::id);
	auto& array = system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	return ExtractComponentStreams<EntityTypeT>(&array, typed_entity_id.entity_id);
}

extern ArrayView<String> entity_type_name_table;
