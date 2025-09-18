#include "BasicMemory.h"
#include <intrin.h>

struct HeapAllocatorBlock {
	u64 size          : 63;
	u64 is_free_block : 1;
	
	HeapAllocatorBlock* last_free_block = nullptr;
	HeapAllocatorBlock* next_free_block = nullptr;
	HeapAllocatorBlock* last_block = nullptr;
	HeapAllocatorBlock* next_block = nullptr;
};
static_assert(sizeof(HeapAllocatorBlock) == 40, "Incorrect HeapAllocatorBlock size.");

static bool IsPowerOfTwo(u64 value) {
	return value != 0 && (value & (value - 1)) == 0;
}

static u64 AlignUp(u64 size, u64 alignment) {
	DebugAssert(IsPowerOfTwo(alignment), "Invalid alignment '0x%llX'. Alignment must be a power of 2.", alignment);
	return (size + alignment - 1) & ~(alignment - 1);
}

static u32 FirstBitLowU32(u32 mask) {
	return _tzcnt_u32(mask);
}

compile_const u64 allocation_granularity = 64 * 1024;
compile_const u64 minimum_alignment = alignof(HeapAllocatorBlock);
compile_const u64 maximum_size      = (1ull << 35);

static_assert(sizeof(HeapAllocator) == 2048, "Incorrect HeapAllocator size.");

// 8 bit float binning introduced by Sebastian Aaltonen in REAC2023 "Modern Mobile Rendering @ HypeHype".
static u32 ComputeBinIndex(u64 size, bool round_up = false) {
	DebugAssert((size % minimum_alignment) == 0, "Allocation size is not aligned to minimum_alignment = %llu.", minimum_alignment);
	DebugAssert(size <= maximum_size, "Allocation size is too large.");
	
	size /= minimum_alignment;
	
	u64 bin_index_u64 = 0;
	double bin_index_float64 = (double)size;
	memcpy(&bin_index_u64, &bin_index_float64, sizeof(u64));
	
	// float64 has 52 explicit mantissa bits, we want only 3 bits.
	// Divide and conditionally round up by 2^49 to get the 3 bit mantissa and 5 bit exponent.
	compile_const u64 denominator   = (1ull << 49);
	compile_const u64 exponent_bias = (1025 << 3);
	
	u64 rounding_offset = round_up ? denominator - 1 : 0;
	u64 bin_index = ((bin_index_u64 + rounding_offset) / denominator) - exponent_bias;
	
	// Resulting float is denormalized when size is under max mantissa value.
	return (u32)(size < 8 ? size : bin_index);
}

static void PushFreeBlock(HeapAllocator* heap, HeapAllocatorBlock* block) {
	u32 bin_index = ComputeBinIndex(block->size);
	u32 index_level_0 = (bin_index >> 3);
	u32 index_level_1 = (bin_index & 0x7);
	
	auto* next_free_block = heap->free_blocks[bin_index];
	if (next_free_block == nullptr) {
		heap->mask_level_0                |= (1u << index_level_0);
		heap->mask_level_1[index_level_0] |= (1u << index_level_1);
	} else {
		next_free_block->last_free_block = block;
	}
	
	block->is_free_block   = true;
	block->last_free_block = nullptr;
	block->next_free_block = next_free_block;
	
	heap->free_blocks[bin_index] = block;
};

static void PopFreeBlock(HeapAllocator* heap, HeapAllocatorBlock* block, u32 bin_index) {
	u32 index_level_0 = (bin_index >> 3);
	u32 index_level_1 = (bin_index & 0x7);
	
	auto* last_free_block = block->last_free_block;
	auto* next_free_block = block->next_free_block;
	
	if (last_free_block == nullptr) {
		heap->free_blocks[bin_index] = next_free_block;
	} else {
		last_free_block->next_free_block = next_free_block;
	}
	
	if (last_free_block == nullptr && next_free_block == nullptr) {
		u8 mask_level_1 = heap->mask_level_1[index_level_0];
		mask_level_1 &= ~(1u << index_level_1);
		heap->mask_level_1[index_level_0] = mask_level_1;
		
		if (mask_level_1 == 0) {
			heap->mask_level_0 &= ~(1u << index_level_0);
		}
	} else if (next_free_block != nullptr) {
		next_free_block->last_free_block = last_free_block;
	}
	
	block->is_free_block   = false;
	block->last_free_block = nullptr;
	block->next_free_block = nullptr;
}

void* HeapAllocator::Allocate(u64 size) {
	size = AlignUp(size, minimum_alignment) + sizeof(HeapAllocatorBlock);
	
	u32 bin_index = ComputeBinIndex(size, true);
	u32 index_level_0 = (bin_index >> 3);
	u32 index_level_1 = (bin_index & 0x7);
	
	u32 larger_size_mask_level_1 = (u32)mask_level_1[index_level_0] & (u32_max << index_level_1);
	if (larger_size_mask_level_1 == 0) {
		u32 larger_size_mask_level_0 = (mask_level_0 & (u32_max << (index_level_0 + 1)));
		if (larger_size_mask_level_0 == 0) return nullptr;
		
		index_level_0 = FirstBitLowU32(larger_size_mask_level_0);
		larger_size_mask_level_1 = (u32)mask_level_1[index_level_0];
	}
	index_level_1 = FirstBitLowU32(larger_size_mask_level_1);
	
	
	u32 free_bin_index = (index_level_0 << 3) | index_level_1;
	auto* block = free_blocks[free_bin_index];
	DebugAssert(block->size >= size, "Block size is too small. %llu/%llu.", block->size, size);
	
	PopFreeBlock(this, block, free_bin_index);
	
	if (block->size - size > sizeof(HeapAllocatorBlock)) {
		auto* new_block = NewInPlace((u8*)block + size, HeapAllocatorBlock);
		new_block->size = block->size - size;
		new_block->is_free_block = false;
		block->size = size;
		
		new_block->next_block = block->next_block;
		new_block->last_block = block;
		
		if (new_block->next_block) {
			new_block->next_block->last_block = new_block;
		}
		block->next_block = new_block;
		
		PushFreeBlock(this, new_block);
	}
	
	return (block + 1);
}

static void CombineFreeBlocks(HeapAllocatorBlock* block_0, HeapAllocatorBlock* block_1) {
	block_0->size      += block_1->size;
	block_0->next_block = block_1->next_block;
	
	if (block_1->next_block) {
		block_1->next_block->last_block = block_0;
	}
}

void HeapAllocator::Deallocate(void* old_memory) {
	if (old_memory == nullptr) return;
	
	auto* block = (HeapAllocatorBlock*)((u8*)old_memory - sizeof(HeapAllocatorBlock));
	
	auto* last_block = block->last_block;
	if (last_block && last_block->is_free_block) {
		PopFreeBlock(this, last_block, ComputeBinIndex(last_block->size));
		CombineFreeBlocks(last_block, block);
		block = last_block;
	}
	
	auto* next_block = block->next_block;
	if (next_block && next_block->is_free_block) {
		PopFreeBlock(this, next_block, ComputeBinIndex(next_block->size));
		CombineFreeBlocks(block, next_block);
	}
	
	PushFreeBlock(this, block);
}


HeapAllocator CreateHeapAllocator(u64 reserved_size) {
	reserved_size = reserved_size < allocation_granularity ? allocation_granularity : AlignUp(reserved_size, allocation_granularity);
	
	auto* memory = SystemAllocateAddressSpace(reserved_size);
	DebugAssert(memory != nullptr, "Failed to reserve virtual address range.");
	
	bool success = SystemCommitMemoryPages(memory, reserved_size);
	DebugAssert(success, "Failed to commit memory pages.");
	
	auto* block = NewInPlace(memory, HeapAllocatorBlock);
	block->size          = reserved_size;
	block->is_free_block = false;
	
	HeapAllocator heap;
	heap.memory = memory;
	PushFreeBlock(&heap, block);
	
	return heap;
}

void ReleaseHeapAllocator(HeapAllocator& heap) {
	bool success = SystemDeallocateAddressSpace(heap.memory);
	DebugAssert(success, "Failed to free virtual address range.");
}
