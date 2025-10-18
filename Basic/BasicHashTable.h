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


compile_const u64 hash_table_max_occupancy_percent = 75;
compile_const u32 hash_table_hash_value_empty   = 0;
compile_const u32 hash_table_hash_value_deleted = 1;
compile_const u32 hash_table_group_size_bits    = 4;
compile_const u32 hash_table_group_size         = 1u << hash_table_group_size_bits;

template<typename KeyT, typename ValueT>
struct HashTableElement {
	KeyT   key;
	ValueT value;
};

template<typename KeyT, typename ValueT>
struct HashTable {
	using ElementType = HashTableElement<KeyT, ValueT>;
	
	u8* metadata = nullptr;
	ElementType* data = nullptr;
	
	u64 capacity = 0;
	u64 count    = 0;
	u64 occupied = 0;
	
	struct Iterator {
		u8* metadata = nullptr;
		ElementType* data = nullptr;
		u64 index    = 0;
		u64 capacity = 0;
		
		Iterator& operator++ () {
			index += 1;
			while (index < capacity && metadata[index] <= hash_table_hash_value_deleted) {
				index += 1;
			}
			return *this;
		}
		
		bool operator!= (const Iterator& other) { return index != other.index; }
		ElementType& operator* () { return data[index]; }
	};
	
	Iterator begin() { return Iterator{ metadata, data, 0,        capacity }; }
	Iterator end()   { return Iterator{ metadata, data, capacity, capacity }; }
};

inline u32 HashTableExtractLargeHash(u64 hash) { return (u32)(hash >> 8); }
inline u32 HashTableExtractSmallHash(u64 hash) { return (u32)((hash & 0xFF) <= hash_table_hash_value_deleted ? (hash & 0xFF) * 2 + 17 : (hash & 0xFF)); }

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableReserve(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc, u64 target_element_count) {
	DebugAssert(hash_table.data == nullptr, "Cannot reserve data in a non empty HashTable.");
	
	u64 new_capacity = target_element_count * 100 / hash_table_max_occupancy_percent;
	new_capacity = new_capacity < hash_table_group_size ? hash_table_group_size : RoundUpToPowerOfTwo(new_capacity);
	
	using ElementType = HashTableElement<KeyT, ValueT>;
	void* memory = alloc->Allocate(new_capacity * sizeof(ElementType) + new_capacity * sizeof(u8), alignof(ElementType));
	
	hash_table.metadata = (u8*)memory;
	hash_table.data     = (ElementType*)((u8*)memory + new_capacity);
	hash_table.capacity = new_capacity;
	hash_table.count    = 0;
	hash_table.occupied = 0;
	
	memset(hash_table.metadata, hash_table_hash_value_empty, new_capacity * sizeof(u8));
}

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableDeallocate(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc) {
	using ElementType = HashTableElement<KeyT, ValueT>;
	alloc->Deallocate(hash_table.metadata, hash_table.capacity * sizeof(ElementType) + hash_table.capacity * sizeof(u8));
}

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableClear(HashTable<KeyT, ValueT>& hash_table) {
	hash_table.count    = 0;
	hash_table.occupied = 0;
	
	memset(hash_table.metadata, hash_table_hash_value_empty, hash_table.capacity * sizeof(u8));
}

template<typename KeyT, typename ValueT, typename AllocatorT>
void HashTableResize(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc, u64 new_capacity) {
	DebugAssert(IsPowerOfTwo(new_capacity), "Invalid HashTable capacity '0x%llX'. HashTable capacity must be a power of 2.", new_capacity);
	
	HashTable<KeyT, ValueT> new_hash_table;
	HashTableReserve(new_hash_table, alloc, new_capacity);
	
	u8* metadata = hash_table.metadata;
	auto* data   = hash_table.data;
	
	for (u64 i = 0; i < hash_table.capacity; i += 1) {
		if (metadata[i] <= hash_table_hash_value_deleted) continue;
		
		auto& element = hash_table.data[i];
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


#define BeginHashTableTraversal()\
	u32 mask = (u32)((hash_table.capacity >> hash_table_group_size_bits) - 1);\
	u64 hash = ComputeHash(key);\
	\
	u32 large_hash = HashTableExtractLargeHash(hash);\
	u32 small_hash = HashTableExtractSmallHash(hash);\
	\
	auto metadata_small_hash = _mm_set1_epi8(small_hash);\
	auto metadata_empty_slot = _mm_set1_epi8(hash_table_hash_value_empty);\
	\
	u8* metadata = hash_table.metadata;\
	auto* data   = hash_table.data;\
	\
	for (u32 i = 0, group_index = (large_hash & mask); i <= mask;) {\
		auto group_metadata = _mm_loadu_si128((__m128i*)metadata + group_index);\
		\
		u32 matching_slot_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(group_metadata, metadata_small_hash));
		
#define EndHashTableTraversal()\
		i += 1;\
		group_index = (group_index + i) & mask;\
	}


template<typename KeyT, typename ValueT>
HashTableAddOrFindResult<KeyT, ValueT> HashTableAddOrFind(HashTable<KeyT, ValueT>& hash_table, const KeyT& key, const ValueT& value) {
	DebugAssert((hash_table.occupied + 1) * 100 <= (hash_table.capacity * hash_table_max_occupancy_percent), "HashTableAddOrFind overflowed maximum hash table occupancy.");
	
	u32 deleted_slot_index = u32_max;
	auto metadata_deleted_slot = _mm_set1_epi8(hash_table_hash_value_deleted);
	
	BeginHashTableTraversal();
		for (u32 bit_index : BitScanLow32(matching_slot_mask)) {
			u32 slot_index = group_index * hash_table_group_size + bit_index;
			
			if (data[slot_index].key == key) {
				return { &data[slot_index], false };
			}
		}
		
		if (deleted_slot_index == u32_max) {
			u32 deleted_slot_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(group_metadata, metadata_deleted_slot));
			if (deleted_slot_mask != 0) {
				deleted_slot_index = group_index * hash_table_group_size + FirstBitLow32(deleted_slot_mask);
			}
		}
		
		u32 empty_slot_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(group_metadata, metadata_empty_slot));
		if (empty_slot_mask != 0) {
			bool use_deleted_slot = (deleted_slot_index != u32_max);
			u32 slot_index = use_deleted_slot ? deleted_slot_index : group_index * hash_table_group_size + FirstBitLow32(empty_slot_mask);
			
			// Deleted slots are already counted as occupied.
			if (use_deleted_slot == false) {
				hash_table.occupied += 1;
			}
			hash_table.count += 1;
			
			metadata[slot_index] = small_hash;
			
			auto& element = data[slot_index];
			element.key   = key;
			element.value = value;
			return { &element, true };
		}
	EndHashTableTraversal();
	
	return {};
}

template<typename KeyT, typename ValueT, typename AllocatorT>
HashTableAddOrFindResult<KeyT, ValueT> HashTableAddOrFind(HashTable<KeyT, ValueT>& hash_table, AllocatorT* alloc, const KeyT& key, const ValueT& value) {
	bool should_grow_hash_table = (hash_table.occupied + 1) * 100 > (hash_table.capacity * hash_table_max_occupancy_percent);
	if (should_grow_hash_table) HashTableResize(hash_table, alloc, hash_table.capacity ? hash_table.capacity * 2 : hash_table_group_size);
	
	return HashTableAddOrFind(hash_table, key, value);
}

template<typename KeyT, typename ValueT>
HashTableElement<KeyT, ValueT>* HashTableFind(HashTable<KeyT, ValueT>& hash_table, const KeyT& key) {
	BeginHashTableTraversal();
		for (u32 bit_index : BitScanLow32(matching_slot_mask)) {
			u32 slot_index = group_index * hash_table_group_size + bit_index;
			if (data[slot_index].key == key) {
				return &data[slot_index];
			}
		}
		
		u32 empty_slot_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(group_metadata, metadata_empty_slot));
		if (empty_slot_mask != 0) {
			return nullptr;
		}
	EndHashTableTraversal();
	
	return nullptr;
}


template<typename KeyT, typename ValueT>
HashTableElement<KeyT, ValueT>* HashTableRemove(HashTable<KeyT, ValueT>& hash_table, const KeyT& key) {
	BeginHashTableTraversal();
		for (u32 bit_index : BitScanLow32(matching_slot_mask)) {
			u32 slot_index = group_index * hash_table_group_size + bit_index;
			
			if (data[slot_index].key == key) {
				u32 not_empty_slot_mask = ~_mm_movemask_epi8(_mm_cmpeq_epi8(group_metadata, metadata_empty_slot));
				
				bool is_full_group = (not_empty_slot_mask & 0xFFFF) == 0xFFFF;
				metadata[slot_index] = is_full_group ? hash_table_hash_value_deleted : hash_table_hash_value_empty;
				
				if (is_full_group == false) {
					hash_table.occupied -= 1;
				}
				hash_table.count -= 1;
				
				return &data[slot_index];
			}
		}
		
		u32 empty_slot_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(group_metadata, metadata_empty_slot));
		if (empty_slot_mask != 0) {
			return nullptr;
		}
	EndHashTableTraversal();
	
	return nullptr;
}

#undef BeginHashTableTraversal
#undef EndHashTableTraversal

