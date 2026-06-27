#ifndef BASICHASHTABLE_HLSL
#define BASICHASHTABLE_HLSL
#include "Basic.hlsl"

compile_const u32 hash_table_retry_count = 8;

struct HashTableKey {
	u64 key;
	u32 hash;
};

struct HashTableFindResult {
	u32 hash_index;
	bool is_found;
};

HashTableFindResult HashTableAddOrFind(RWStructuredBuffer<u64> keys, HashTableKey key, u32 hash_table_size, u32 hash_table_offset = 0) {
	u32 hash_table_size_mask = hash_table_size - 1;
	
	HashTableFindResult result;
	result.hash_index = key.hash & hash_table_size_mask;
	result.is_found   = false;
	
	[allow_uav_condition]
	for (u32 i = 0; i < hash_table_retry_count; i += 1) {
		u64 original_value;
		InterlockedCompareExchange(keys[result.hash_index + hash_table_offset], (u64)0, key.key, original_value);
		
		if (original_value == 0 || original_value == key.key) {
			result.is_found = true;
			break;
		}
		
		result.hash_index = (result.hash_index + 1) & hash_table_size_mask;
	}
	
	return result;
}

HashTableFindResult HashTableFind(RWStructuredBuffer<u64> keys, HashTableKey key, u32 hash_table_size, u32 hash_table_offset = 0) {
	u32 hash_table_size_mask = hash_table_size - 1;
	
	HashTableFindResult result;
	result.hash_index = key.hash & hash_table_size_mask;
	result.is_found   = false;
	
	[allow_uav_condition]
	for (u32 i = 0; i < hash_table_retry_count; i += 1) {
		u64 original_value = keys[result.hash_index];
		
		if (original_value == key.key) {
			result.is_found = true;
			break;
		}
		
		result.hash_index = (result.hash_index + 1) & hash_table_size_mask;
	}
	
	return result;
}

struct HashCellSize {
	float hash_cell_size;
	uint level_of_detail;
};

HashCellSize QuantizeHashCellSize(float hash_cell_size, float min_hash_cell_size, float inv_min_hash_cell_size) {
	float level_of_detail = floor(max(log2(hash_cell_size * inv_min_hash_cell_size), 0.0));
	
	HashCellSize result;
	result.hash_cell_size  = exp2(level_of_detail) * min_hash_cell_size;
	result.level_of_detail = (u32)level_of_detail;
	
	return result;
}

#endif // BASICHASHTABLE_HLSL
