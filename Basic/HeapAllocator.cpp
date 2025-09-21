#include "BasicMemory.h"
#include "BasicMath.h"

compile_const u64 allocation_granularity = 64 * 1024;
compile_const u64 minimum_alignment_bits = 3;
compile_const u64 minimum_alignment = (1ull << minimum_alignment_bits);
compile_const u64 maximum_size      = (1ull << 31) - 1; // Limited by block sizes in HeapAllocatorBlockHeader.
static_assert(sizeof(HeapAllocator) == 1712, "Incorrect HeapAllocator size.");

struct HeapAllocatorBlockHeader {
	u32 is_free_block   : 1;
	u32 size            : 31;
	
	u32 has_next_block  : 1;
	u32 last_block_size : 31;
	
	void SetLastBlock(HeapAllocatorBlock* last_block) { last_block_size = last_block ? (u8*)this - (u8*)last_block : 0; }
	void SetNextBlock(HeapAllocatorBlock* next_block) { has_next_block  = (next_block != nullptr); }
	
	HeapAllocatorBlock* GetLastBlock() { return HasLastBlock() ? (HeapAllocatorBlock*)((u8*)this - last_block_size) : nullptr; }
	HeapAllocatorBlock* GetNextBlock() { return HasNextBlock() ? (HeapAllocatorBlock*)((u8*)this + size)            : nullptr; }
	
	bool HasLastBlock() { return last_block_size != 0; }
	bool HasNextBlock() { return has_next_block  != 0; }
};
static_assert(sizeof(HeapAllocatorBlockHeader) == 8, "Incorrect HeapAllocatorBlockHeader size.");

struct HeapAllocatorBlock : HeapAllocatorBlockHeader {
	HeapAllocatorBlock* last_free_block = nullptr;
	HeapAllocatorBlock* next_free_block = nullptr;
};
static_assert(sizeof(HeapAllocatorBlock) == 24, "Incorrect HeapAllocatorBlock size.");

struct HeapAllocatorPage {
	HeapAllocatorPage* last_page = nullptr;
};
static_assert(sizeof(HeapAllocatorPage) == 8, "Incorrect HeapAllocatorPage size.");


// 8 bit float binning introduced by Sebastian Aaltonen in REAC2023 "Modern Mobile Rendering @ HypeHype".
static u32 ComputeBinIndex(u64 size, bool round_up = false) {
	DebugAssert((size % minimum_alignment) == 0, "Allocation size is not aligned to the minimum_alignment = %llu.", minimum_alignment);
	DebugAssert((size <= maximum_size), "Allocation size is too large. %llu/%llu.", size, maximum_size);
	
	u32 size_index = (u32)(size >> minimum_alignment_bits);
	u64 bin_index_u64 = 0;
	
	double bin_index_float64 = (double)size_index;
	memcpy(&bin_index_u64, &bin_index_float64, sizeof(u64));
	
	// float64 has 52 explicit mantissa bits, we want only 3 bits.
	// Divide and conditionally round up by 2^49 to get the 3 bit mantissa and 5 bit exponent.
	compile_const u64 denominator   = (1ull << 49);
	compile_const u64 exponent_bias = (1025 << 3);
	
	u64 rounding_offset = round_up ? denominator - 1 : 0;
	u64 bin_index = ((bin_index_u64 + rounding_offset) / denominator) - exponent_bias;
	
	// Resulting float is denormalized when the size_index is under max mantissa value.
	return size_index < 8 ? size_index : (u32)bin_index;
}

static u64 AlignToNextBinSize(u64 size) {
	DebugAssert((size % minimum_alignment) == 0, "Allocation size is not aligned to the minimum_alignment = %llu.", minimum_alignment);
	DebugAssert((size <= maximum_size), "Allocation size is too large. %llu/%llu.", size, maximum_size);
	
	u32 size_index = (u32)(size >> minimum_alignment_bits);
	u64 bin_index_u64 = 0;
	
	double bin_index_float64 = (double)size_index;
	memcpy(&bin_index_u64, &bin_index_float64, sizeof(u64));
	
	// Round up the low 49 bits of the mantissa.
	bin_index_u64 = AlignUp(bin_index_u64, (1ull << 49));
	
	memcpy(&bin_index_float64, &bin_index_u64, sizeof(u64));
	size_index = (u32)bin_index_float64;
	
	return (u64)size_index << minimum_alignment_bits;
}

static void PushFreeBlock(HeapAllocator* heap, HeapAllocatorBlock* block) {
	DebugAssert(block->size >= sizeof(HeapAllocatorBlock), "Block is too small to be cast to HeapAllocatorBlock.");
	
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

static void PushFreeBlockExcess(HeapAllocator* heap, HeapAllocatorBlock* block, u64 size) {
	if (block->size - size < sizeof(HeapAllocatorBlock)) return;
	
	auto* new_block = NewInPlace((u8*)block + size, HeapAllocatorBlock);
	new_block->size = block->size - size;
	new_block->is_free_block = false;
	block->size = size;
	
	new_block->SetNextBlock(block->GetNextBlock());
	new_block->SetLastBlock(block);
	
	if (new_block->HasNextBlock()) {
		new_block->GetNextBlock()->SetLastBlock(new_block);
	}
	block->SetNextBlock(new_block);
	
	PushFreeBlock(heap, new_block);
}

static void CombineFreeBlocks(HeapAllocatorBlock* block_0, HeapAllocatorBlock* block_1) {
	block_0->size += block_1->size;
	block_0->SetNextBlock(block_1->GetNextBlock());
	
	if (block_1->HasNextBlock()) {
		block_1->GetNextBlock()->SetLastBlock(block_0);
	}
}

static void AllocateNewPage(HeapAllocator* heap, u64 reserved_size) {
	reserved_size = reserved_size < allocation_granularity ? allocation_granularity : AlignUp(reserved_size, allocation_granularity);
	
	auto* memory = SystemAllocateAddressSpace(reserved_size);
	DebugAssert(memory != nullptr, "Failed to reserve virtual address range.");
	
	bool success = SystemCommitMemoryPages(memory, reserved_size);
	DebugAssert(success, "Failed to commit memory pages.");
	
	auto* page = NewInPlace(memory, HeapAllocatorPage);
	page->last_page = heap->current_page;
	heap->current_page = page;
	
	auto* block = NewInPlace(page + 1, HeapAllocatorBlock);
	block->size = reserved_size - sizeof(HeapAllocatorPage);
	block->is_free_block   = false;
	block->last_block_size = 0;
	block->has_next_block  = false;
	PushFreeBlock(heap, block);
}

static void* AllocateFromHeap(HeapAllocator* heap, u64 size) {
	size = Max(AlignUp(size, minimum_alignment) + sizeof(HeapAllocatorBlockHeader), sizeof(HeapAllocatorBlock));
	
	u32 bin_index = ComputeBinIndex(size, true);
	u32 index_level_0 = (bin_index >> 3);
	u32 index_level_1 = (bin_index & 0x7);
	
	u32 larger_size_mask_level_1 = (u32)heap->mask_level_1[index_level_0] & (u32_max << index_level_1);
	if (larger_size_mask_level_1 == 0) {
		u32 larger_size_mask_level_0 = (heap->mask_level_0 & (u32_max << (index_level_0 + 1)));
		if (larger_size_mask_level_0 == 0) return nullptr;
		
		index_level_0 = FirstBitLow32(larger_size_mask_level_0);
		larger_size_mask_level_1 = (u32)heap->mask_level_1[index_level_0];
	}
	index_level_1 = FirstBitLow32(larger_size_mask_level_1);
	
	
	u32 free_bin_index = (index_level_0 << 3) | index_level_1;
	auto* block = heap->free_blocks[free_bin_index];
	DebugAssert(block->size >= size, "Block size is too small. %llu/%llu.", block->size, size);
	
	PopFreeBlock(heap, block, free_bin_index);
	PushFreeBlockExcess(heap, block, size);
	
	return (u8*)block + sizeof(HeapAllocatorBlockHeader);
}

void* HeapAllocator::Allocate(u64 size, u64 alignment) {
	if (size == 0) return nullptr;
	DebugAssert(alignment <= minimum_alignment, "Unsupported alignment.");
	
	auto* memory = AllocateFromHeap(this, size);
	if (memory != nullptr) return memory;
	
	u64 new_block_committed_size = AlignToNextBinSize(size + sizeof(HeapAllocatorBlockHeader)) + sizeof(HeapAllocatorPage);
	u64 new_block_reserved_size  = reserved_size > new_block_committed_size ? reserved_size : new_block_committed_size;
	AllocateNewPage(this, new_block_reserved_size);
	
	return AllocateFromHeap(this, size);
}

void* HeapAllocator::Reallocate(void* old_memory, u64 old_size, u64 new_size, u64 alignment) {
	if (old_memory == nullptr) return Allocate(new_size, alignment);
	if (new_size == 0)         return Deallocate(old_memory, old_size), nullptr;
	if (new_size <= old_size)  return old_memory;
	
	DebugAssert(alignment <= minimum_alignment, "Unsupported alignment.");
	new_size = Max(AlignUp(new_size, minimum_alignment) + sizeof(HeapAllocatorBlockHeader), sizeof(HeapAllocatorBlock));
	
	auto* block = (HeapAllocatorBlock*)((u8*)old_memory - sizeof(HeapAllocatorBlockHeader));
	
	auto* next_block = block->GetNextBlock();
	if (next_block && next_block->is_free_block) {
		PopFreeBlock(this, next_block, ComputeBinIndex(next_block->size));
		CombineFreeBlocks(block, next_block);
		
		if (block->size >= new_size) {
			PushFreeBlockExcess(this, block, new_size);
			return (u8*)block + sizeof(HeapAllocatorBlockHeader);
		}
	}
	
	auto* last_block = block->GetLastBlock();
	if (last_block && last_block->is_free_block) {
		PopFreeBlock(this, last_block, ComputeBinIndex(last_block->size));
		CombineFreeBlocks(last_block, block);
		block = last_block;
		
		if (block->size >= new_size) {
			PushFreeBlockExcess(this, block, new_size);
			memmove((u8*)block + sizeof(HeapAllocatorBlockHeader), old_memory, old_size);
			return (u8*)block + sizeof(HeapAllocatorBlockHeader);
		}
	}
	
	void* new_memory = Allocate(new_size, alignment);
	memcpy(new_memory, old_memory, old_size);
	
	// Must happen after we copy old_memory, otherwise we would override the first 16 bytes.
	PushFreeBlock(this, block);
	
	return new_memory;
}

void HeapAllocator::Deallocate(void* old_memory, u64 old_size) {
	if (old_memory == nullptr) return;
	
	auto* block = (HeapAllocatorBlock*)((u8*)old_memory - sizeof(HeapAllocatorBlockHeader));
	
	auto* next_block = block->GetNextBlock();
	if (next_block && next_block->is_free_block) {
		PopFreeBlock(this, next_block, ComputeBinIndex(next_block->size));
		CombineFreeBlocks(block, next_block);
	}
	
	auto* last_block = block->GetLastBlock();
	if (last_block && last_block->is_free_block) {
		PopFreeBlock(this, last_block, ComputeBinIndex(last_block->size));
		CombineFreeBlocks(last_block, block);
		block = last_block;
	}
	
	PushFreeBlock(this, block);
}


HeapAllocator CreateHeapAllocator(u64 reserved_size) {
	DebugAssert(reserved_size <= maximum_size, "Reserved size is too large. %llu/%llu.", reserved_size, maximum_size);
	
	HeapAllocator heap;
	heap.reserved_size = (u32)reserved_size;
	AllocateNewPage(&heap, reserved_size);
	
	return heap;
}

void ReleaseHeapAllocator(HeapAllocator& heap) {
	auto* page = heap.current_page;
	heap.current_page = nullptr;
	
	while (page != nullptr) {
		auto* last_page = page->last_page;
		
		bool success = SystemDeallocateAddressSpace(page);
		DebugAssert(success, "Failed to free virtual address range.");
		
		page = last_page;
	}
}

u64 HeapAllocator::ComputeTotalMemoryUsage() {
	auto* page = current_page;
	
	u64 total_memory_usage = 0;
	while (page != nullptr) {
		auto* last_page = page->last_page;
		
		auto* block = (HeapAllocatorBlock*)(page + 1);
		while (block != nullptr) {
			total_memory_usage += block->is_free_block ? 0 : block->size;
			block = block->GetNextBlock();
		}
		
		page = last_page;
	}
	
	return total_memory_usage;
}

