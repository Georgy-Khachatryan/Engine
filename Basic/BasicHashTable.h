#pragma once
#include "Basic.h"
#include "BasicArray.h"
#include "BasicMath.h"
#include "BasicMemory.h"


// Based on wyhash64 by Wang Yi (public domain).
inline u64 ComputeHash64(u64 A, u64 B = 0x4B33A62ED433D4A3ull) {
	A ^= 0x2D358DCCAA6C78A5ull;
	B ^= 0x8BB84B93962EACC9ull;
	A = _mulx_u64(A, B, &B);
	
	A ^= 0x2D358DCCAA6C78A5ull;
	B ^= 0x8BB84B93962EACC9ull;
	A = _mulx_u64(A, B, &B);
	
	return A ^ B;
}

inline u64 ComputeHash(u8  value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(s8  value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(u16 value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(s16 value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(u32 value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(s32 value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(u64 value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(s64 value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(bool value) { return ComputeHash64((u64)value); }
inline u64 ComputeHash(const void* value) { return ComputeHash64((u64)value); }

template<typename KeyT, typename ValueT>
struct HashTableElement {
	u64    hash;
	KeyT   key;
	ValueT value;
};

template<typename KeyT, typename ValueT>
struct HashTable {
	using ElementType = HashTableElement<KeyT, ValueT>;
	
	ElementType* data = nullptr;
	
	u64 capacity = 0;
	u64 count    = 0;
	u64 occupied = 0;
};

compile_const u64 hash_table_max_occupancy_percent = 75;
compile_const u64 hash_table_hash_value_available  = 0;
compile_const u64 hash_table_hash_value_deleted    = 1;
compile_const u64 hash_table_min_capacity_elements = 16;

inline u64 HashTableFilterHash(u64 hash) {
	return hash <= hash_table_hash_value_deleted ? hash * 2 + 17 : hash;
}

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableReserve(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc, u64 target_element_count) {
	DebugAssert(hash_table.data == nullptr, "Cannot reserve data in a non empty HashTable.");
	
	u64 new_capacity = target_element_count * 100 / hash_table_max_occupancy_percent;
	new_capacity = new_capacity < hash_table_min_capacity_elements ? hash_table_min_capacity_elements : RoundUpToPowerOfTwo(new_capacity);
	
	using ElementType = HashTableElement<KeyT, ValueT>;
	hash_table.data     = (ElementType*)alloc->Allocate(new_capacity * sizeof(ElementType), alignof(ElementType));
	hash_table.capacity = new_capacity;
	hash_table.count    = 0;
	hash_table.occupied = 0;
	
	memset(hash_table.data, 0, new_capacity * sizeof(ElementType));
}

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableDeallocate(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc) {
	using ElementType = HashTableElement<KeyT, ValueT>;
	alloc->Deallocate(hash_table.data, hash_table.capacity * sizeof(ElementType));
}

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableResize(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc, u64 new_capacity) {
	HashTable<KeyT, ValueT> new_hash_table;
	HashTableReserve(new_hash_table, alloc, new_capacity);
	
	for (u64 i = 0; i < hash_table.capacity; i += 1) {
		auto& element = hash_table.data[i];
		if (element.hash <= hash_table_hash_value_deleted) continue;
		
		HashTableAddOrFind(new_hash_table, element.key, element.value);
	}
	
	HashTableDeallocate(hash_table, alloc);
	hash_table = new_hash_table;
}

template<typename KeyT, typename ValueT>
struct HashTableAddOrFindResult {
	HashTableElement<KeyT, ValueT>* element = nullptr;
	bool is_added = false;
};

template<typename KeyT, typename ValueT>
HashTableAddOrFindResult<KeyT, ValueT> HashTableAddOrFind(HashTable<KeyT, ValueT>& hash_table, const KeyT& key, const ValueT& value) {
	DebugAssert((hash_table.occupied + 1) * 100 <= (hash_table.capacity * hash_table_max_occupancy_percent), "HashTableAddOrFind overflowed maximum hash table occupancy.");
	
	u64 mask = (hash_table.capacity - 1);
	u64 hash = HashTableFilterHash(ComputeHash(key));
	
	for (u64 i = 0, index = hash & mask; i <= mask; i += 1) {
		auto& element = hash_table.data[index];
		
		if (element.hash == hash && element.key == key) {
			return { &element, false };
		}
		
		if (element.hash <= hash_table_hash_value_deleted) {
			// Deleted slots already count as occupied.
			if (element.hash == hash_table_hash_value_available) hash_table.occupied += 1;
			hash_table.count += 1;
			
			element.hash  = hash;
			element.key   = key;
			element.value = value;
			return { &element, true };
		}
		
		index = (index + 1 + i) & mask;
	}
	
	return {};
}

template<typename KeyT, typename ValueT, typename AllocatorT>
HashTableAddOrFindResult<KeyT, ValueT> HashTableAddOrFind(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc, const KeyT& key, const ValueT& value) {
	bool should_grow_hash_table = (hash_table.occupied + 1) * 100 > (hash_table.capacity * hash_table_max_occupancy_percent);
	if (should_grow_hash_table) HashTableResize(hash_table, alloc, hash_table.capacity ? hash_table.capacity * 2 : 16);
	
	return HashTableAddOrFind(hash_table, key, value);
}

template<typename KeyT, typename ValueT>
HashTableElement<KeyT, ValueT>* HashTableFind(HashTable<KeyT, ValueT>& hash_table, const KeyT& key) {
	u64 mask = (hash_table.capacity - 1);
	u64 hash = HashTableFilterHash(ComputeHash(key));
	
	for (u64 i = 0, index = hash & mask; i <= mask; i += 1) {
		auto& element = hash_table.data[index];
		
		if (element.hash == hash && element.key == key) {
			return &element;
		}
		
		if (element.hash == hash_table_hash_value_available) {
			return nullptr;
		}
		
		index = (index + 1 + i) & mask;
	}
	
	return nullptr;
}

template<typename KeyT, typename ValueT>
HashTableElement<KeyT, ValueT>* HashTableRemove(HashTable<KeyT, ValueT>& hash_table, const KeyT& key) {
	u64 mask = (hash_table.capacity - 1);
	u64 hash = HashTableFilterHash(ComputeHash(key));
	
	for (u64 i = 0, index = hash & mask; i <= mask; i += 1) {
		auto& element = hash_table.data[index];
		
		if (element.hash == hash && element.key == key) {
			element.hash = hash_table_hash_value_deleted;
			hash_table.count -= 1;
			return &element;
		}
		
		if (element.hash == hash_table_hash_value_available) {
			return nullptr;
		}
		
		index = (index + 1 + i) & mask;
	}
	
	return nullptr;
}

template<typename KeyT, typename ValueT>
void HashTableRemove(HashTable<KeyT, ValueT>& hash_table, HashTableElement<KeyT, ValueT>* element) {
	if (element == nullptr) return;
	
	element.hash = hash_table_hash_value_deleted;
	hash_table.count -= 1;
}

