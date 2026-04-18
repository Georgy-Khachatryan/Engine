#include "BasicMemory.h"
#include "BasicMath.h"

compile_const u64 allocation_granularity = 64 * 1024;
compile_const u64 minimum_alignment_bits = 3;
compile_const u64 minimum_alignment = (1ull << minimum_alignment_bits);
compile_const u64 maximum_size      = (1ull << 31) - 1; // Limited by block sizes in HeapAllocatorBlockHeader.

// UMA Heap allocator block. Stored intrusively with the allocation.
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
	
	compile_const u64 minimum_block_size = 24;
};
static_assert(sizeof(HeapAllocatorBlock) == HeapAllocatorBlock::minimum_block_size, "Incorrect HeapAllocatorBlock size.");

struct HeapAllocatorPage {
	HeapAllocatorPage* last_page = nullptr;
	u64 size = 0;
};
static_assert(sizeof(HeapAllocatorPage) == 16, "Incorrect HeapAllocatorPage size.");

static HeapAllocatorBlock* CreateNewBlock(HeapAllocator* heap, HeapAllocatorBlock* block, u64 size) {
	return NewInPlace((u8*)block + size, HeapAllocatorBlock);
}

static void ReleaseOldBlock(HeapAllocator* heap, HeapAllocatorBlock* block) {
	// Nothing to do here.
}


// NUMA Heap allocator block. Stored in a separate array of blocks.
struct NumaHeapAllocatorBlock {
	u32 is_free_block : 1;
	u32 size          : 31;
	
	u32 offset = 0;
	
	NumaHeapAllocatorBlock* last_block = nullptr;
	NumaHeapAllocatorBlock* next_block = nullptr;
	
	NumaHeapAllocatorBlock* last_free_block = nullptr;
	NumaHeapAllocatorBlock* next_free_block = nullptr;
	
	compile_const u64 minimum_block_size = 1;
	
	void SetLastBlock(NumaHeapAllocatorBlock* new_last_block) { last_block = new_last_block; }
	void SetNextBlock(NumaHeapAllocatorBlock* new_next_block) { next_block = new_next_block; }
	
	NumaHeapAllocatorBlock* GetLastBlock() { return last_block; }
	NumaHeapAllocatorBlock* GetNextBlock() { return next_block; }
	
	bool HasLastBlock() { return last_block != nullptr; }
	bool HasNextBlock() { return next_block != nullptr; }
};
static_assert(sizeof(NumaHeapAllocatorBlock) == 40, "Incorrect NumaHeapAllocatorPage size."); // TODO: Could use relative pointers.

static NumaHeapAllocatorBlock* CreateNewBlock(NumaHeapAllocator* heap, NumaHeapAllocatorBlock* block, u64 size) {
	DebugAssert(heap->unused_block_count != 0, "Trying to allocate more blocks than the limit of '%'.", heap->max_allocation_count);
	u32 new_block_index = heap->unused_block_indices[--heap->unused_block_count];
	
	auto* new_block = &heap->blocks[new_block_index];
	new_block->offset = block->offset + (u32)size;
	
	return new_block;
}

static void ReleaseOldBlock(NumaHeapAllocator* heap, NumaHeapAllocatorBlock* block) {
	heap->unused_block_indices[heap->unused_block_count++] = (u32)(block - heap->blocks);
}


// 8 bit float binning introduced by Sebastian Aaltonen in REAC2023 "Modern Mobile Rendering @ HypeHype".
static u32 ComputeBinIndex(u64 size, bool round_up = false) {
	DebugAssert((size % minimum_alignment) == 0, "Allocation size is not aligned to the minimum_alignment = %..", minimum_alignment);
	DebugAssert((size <= maximum_size), "Allocation size is too large. (%/%).", size, maximum_size);
	
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
	DebugAssert((size % minimum_alignment) == 0, "Allocation size is not aligned to the minimum_alignment = %..", minimum_alignment);
	DebugAssert((size <= maximum_size), "Allocation size is too large. (%/%).", size, maximum_size);
	
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

template<typename HeapAllocatorT, typename HeapAllocatorBlockT>
static void PushFreeBlock(HeapAllocatorT* heap, HeapAllocatorBlockT* block) {
	DebugAssert(block->size >= HeapAllocatorBlockT::minimum_block_size, "Block is too small to be cast to HeapAllocatorBlock.");
	
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

template<typename HeapAllocatorT, typename HeapAllocatorBlockT>
static void PopFreeBlock(HeapAllocatorT* heap, HeapAllocatorBlockT* block, u32 bin_index) {
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

template<typename HeapAllocatorT, typename HeapAllocatorBlockT>
static void PushFreeBlockExcess(HeapAllocatorT* heap, HeapAllocatorBlockT* block, u64 size) {
	if (block->size - size < HeapAllocatorBlockT::minimum_block_size) return;
	
	auto* new_block = CreateNewBlock(heap, block, size);
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

template<typename HeapAllocatorT, typename HeapAllocatorBlockT>
static void CombineFreeBlocks(HeapAllocatorT* heap, HeapAllocatorBlockT* block_0, HeapAllocatorBlockT* block_1) {
	block_0->size += block_1->size;
	block_0->SetNextBlock(block_1->GetNextBlock());
	
	if (block_1->HasNextBlock()) {
		block_1->GetNextBlock()->SetLastBlock(block_0);
	}
	
	ReleaseOldBlock(heap, block_1);
}

template<typename HeapAllocatorT>
static typename HeapAllocatorT::BlockType* AllocateHeapBlock(HeapAllocatorT* heap, u64 size) {
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
	DebugAssert(block->size >= size, "Block size is too small. (%/%).", block->size, size);
	
	PopFreeBlock(heap, block, free_bin_index);
	PushFreeBlockExcess(heap, block, size);
	
	return block;
}

template<typename HeapAllocatorT, typename HeapAllocatorBlockT>
static void DeallocateHeapBlock(HeapAllocatorT* heap, HeapAllocatorBlockT* block) {
	auto* next_block = block->GetNextBlock();
	if (next_block && next_block->is_free_block) {
		PopFreeBlock(heap, next_block, ComputeBinIndex(next_block->size));
		CombineFreeBlocks(heap, block, next_block);
	}
	
	auto* last_block = block->GetLastBlock();
	if (last_block && last_block->is_free_block) {
		PopFreeBlock(heap, last_block, ComputeBinIndex(last_block->size));
		CombineFreeBlocks(heap, last_block, block);
		block = last_block;
	}
	
	PushFreeBlock(heap, block);
}


static void PushNewPageFreeBlock(HeapAllocator* heap, HeapAllocatorPage* page, u64 reserved_size) {
	auto* block = NewInPlace(page + 1, HeapAllocatorBlock);
	block->size = reserved_size - sizeof(HeapAllocatorPage);
	block->is_free_block   = false;
	block->last_block_size = 0;
	block->has_next_block  = false;
	PushFreeBlock(heap, block);
}

static void AllocateNewPage(HeapAllocator* heap, u64 reserved_size) {
	ProfilerScope("HeapAllocator::AllocateNewPage");
	
	reserved_size = reserved_size < allocation_granularity ? allocation_granularity : AlignUp(reserved_size, allocation_granularity);
	
	auto* memory = SystemAllocateAddressSpace(reserved_size);
	DebugAssert(memory != nullptr, "Failed to reserve virtual address range.");
	
	bool success = SystemCommitMemoryPages(memory, reserved_size);
	DebugAssert(success, "Failed to commit memory pages.");
	
	auto* page = NewInPlace(memory, HeapAllocatorPage);
	page->last_page = heap->current_page;
	page->size      = reserved_size;
	heap->current_page = page;
	
	PushNewPageFreeBlock(heap, page, reserved_size);
}

static u64 ComputeBlockSize(u64 size, u64 alignment) {
	return Math::Max(AlignUp(size + (alignment - minimum_alignment), minimum_alignment) + sizeof(HeapAllocatorBlockHeader), sizeof(HeapAllocatorBlock));
}

static HeapAllocatorBlock* AllocatorBlockFromMemory(void* memory) {
	auto* block = (HeapAllocatorBlock*)((u8*)memory - sizeof(HeapAllocatorBlockHeader));
	
	// Zero size signals that this is a padding block. The real block is stored at an offset stored in last_block_size.
	if (block->size == 0) {
		block = (HeapAllocatorBlock*)((u8*)block - block->last_block_size);
	}
	
	return block;
}

static void* MemoryFromAllocatorBlock(HeapAllocatorBlock* block, u64 alignment) {
	u8* memory = (u8*)block + sizeof(HeapAllocatorBlockHeader);
	u8* aligned_memory = (u8*)AlignUp((u64)memory, alignment);
	
	// Insert a padding block with an offset to the real block.
	if (aligned_memory != memory) {
		auto* padding_block = NewInPlace((u8*)aligned_memory - sizeof(HeapAllocatorBlockHeader), HeapAllocatorBlockHeader);
		padding_block->is_free_block   = false;
		padding_block->size            = 0; // Zero size signals that this is a padding block.
		padding_block->has_next_block  = false;
		padding_block->last_block_size = (u8*)padding_block - (u8*)block; // Offset to the real block.
	}
	
	return aligned_memory;
}

void* HeapAllocator::Allocate(u64 size, u64 alignment) {
	if (size == 0) return nullptr;
	
	alignment = Math::Max(alignment, minimum_alignment);
	u64 block_size = ComputeBlockSize(size, alignment);
	
	auto* block = AllocateHeapBlock(this, block_size);
	if (block != nullptr) return MemoryFromAllocatorBlock(block, alignment);
	
	u64 new_block_committed_size = AlignToNextBinSize(block_size) + sizeof(HeapAllocatorPage);
	u64 new_block_reserved_size  = reserved_size > new_block_committed_size ? reserved_size : new_block_committed_size;
	AllocateNewPage(this, new_block_reserved_size);
	
	block = AllocateHeapBlock(this, block_size);
	return MemoryFromAllocatorBlock(block, alignment);
}

void HeapAllocator::Deallocate(void* old_memory, u64 old_size) {
	if (old_memory == nullptr) return;
	
	auto* block = AllocatorBlockFromMemory(old_memory);
	DeallocateHeapBlock(this, block);
}

void* HeapAllocator::Reallocate(void* old_memory, u64 old_size, u64 new_size, u64 alignment) {
	if (old_memory == nullptr) return Allocate(new_size, alignment);
	if (new_size == 0)         return Deallocate(old_memory, old_size), nullptr;
	if (new_size <= old_size)  return old_memory;
	
	alignment = Math::Max(alignment, minimum_alignment);
	u64 new_block_size = ComputeBlockSize(new_size, alignment);
	
	auto* block = AllocatorBlockFromMemory(old_memory);
	
	auto* next_block = block->GetNextBlock();
	if (next_block && next_block->is_free_block) {
		PopFreeBlock(this, next_block, ComputeBinIndex(next_block->size));
		CombineFreeBlocks(this, block, next_block);
		
		if (block->size >= new_block_size) {
			PushFreeBlockExcess(this, block, new_block_size);
			return old_memory;
		}
	}
	
	auto* last_block = block->GetLastBlock();
	if (last_block && last_block->is_free_block) {
		PopFreeBlock(this, last_block, ComputeBinIndex(last_block->size));
		CombineFreeBlocks(this, last_block, block);
		block = last_block;
		
		if (block->size >= new_block_size) {
			void* new_memory = MemoryFromAllocatorBlock(block, alignment);
			memmove(new_memory, old_memory, old_size);
			
			// Must happen after we copy the old_memory, otherwise we could override it's range when placing a free block.
			PushFreeBlockExcess(this, block, new_block_size);
			
			return new_memory;
		}
	}
	
	void* new_memory = Allocate(new_size, alignment);
	memcpy(new_memory, old_memory, old_size);
	
	// Must happen after we copy the old_memory, otherwise we would override the first 16 bytes.
	PushFreeBlock(this, block);
	
	return new_memory;
}

u64 HeapAllocator::GetMemoryBlockSize(void* memory) {
	if (memory == nullptr) return 0;
	
	auto* block = AllocatorBlockFromMemory(memory);
	return (u8*)block + block->size - (u8*)memory;
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

HeapAllocator CreateHeapAllocator(u64 reserved_size) {
	DebugAssert(reserved_size <= maximum_size, "Reserved size is too large. (%/%).", reserved_size, maximum_size);
	
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

void ResetHeapAllocator(HeapAllocator& heap) {
	heap.mask_level_0 = 0;
	memset(heap.mask_level_1, 0, sizeof(heap.mask_level_1));
	memset(heap.free_blocks, 0, sizeof(heap.free_blocks));
	
	auto* page = heap.current_page;
	while (page != nullptr) {
		PushNewPageFreeBlock(&heap, page, page->size);
		page = page->last_page;
	}
}


NumaHeapAllocation NumaHeapAllocator::Allocate(u64 size) {
	if (size == 0) return NumaHeapAllocation{};
	
	u64 block_size = AlignUp(size, minimum_alignment);
	auto* block = AllocateHeapBlock(this, block_size);
	
	return { block == nullptr ? u32_max : (u32)(block - blocks) };
}

void NumaHeapAllocator::ReallocateShrink(NumaHeapAllocation allocation, u64 new_size) {
	DebugAssert(allocation.index != u32_max, "Invalid allocation.");
	
	auto* block = &blocks[allocation.index];
	PushFreeBlockExcess(this, block, new_size);
}

void NumaHeapAllocator::Deallocate(NumaHeapAllocation allocation) {
	if (allocation.index == u32_max) return;
	
	auto* block = &blocks[allocation.index];
	DeallocateHeapBlock(this, block);
}

u64 NumaHeapAllocator::GetMemoryBlockOffset(NumaHeapAllocation allocation) {
	return allocation.index != u32_max ? blocks[allocation.index].offset : u32_max;
}

u64 NumaHeapAllocator::GetMemoryBlockSize(NumaHeapAllocation allocation) {
	return allocation.index != u32_max ? blocks[allocation.index].size : 0;
}

float NumaHeapAllocator::ComputeFragmentation() {
	float64 quality = 0.0;
	float64 total_free_size = 0.0;
	
	for (u32 i = 0; i < max_allocation_count; i += 1) {
		auto& block = blocks[i];
		if (block.is_free_block) {
			auto block_size = (float64)block.size;
			quality         += block_size * block_size;
			total_free_size += block_size;
		}
	}
	
	// For reference see https://asawicki.info/news_1757_a_metric_for_memory_fragmentation
	auto quality_percent = total_free_size != 0.0 ? sqrt(quality) / total_free_size : 1.0;
	return (float)(1.0 - (quality_percent * quality_percent));
}

NumaHeapAllocator CreateNumaHeapAllocator(StackAllocator* alloc, u32 max_allocation_count, u32 reserved_size) {
	NumaHeapAllocator heap;
	heap.blocks = (NumaHeapAllocatorBlock*)alloc->Allocate(max_allocation_count * sizeof(NumaHeapAllocatorBlock), alignof(NumaHeapAllocatorBlock));
	heap.unused_block_indices = (u32*)alloc->Allocate(max_allocation_count * sizeof(u32), alignof(u32));
	heap.max_allocation_count = max_allocation_count;
	heap.reserved_size        = reserved_size;
	
	ResetNumaHeapAllocator(heap);
	
	return heap;
}

static void PushNewPageFreeBlock(NumaHeapAllocator* heap, u32 reserved_size) {
	u32 new_block_index = heap->unused_block_indices[--heap->unused_block_count];
	
	auto* block = &heap->blocks[new_block_index];
	memset(block, 0, sizeof(NumaHeapAllocatorBlock));
	
	block->size = reserved_size;
	PushFreeBlock(heap, block);
}

void ResetNumaHeapAllocator(NumaHeapAllocator& heap) {
	heap.mask_level_0 = 0;
	memset(heap.mask_level_1, 0, sizeof(heap.mask_level_1));
	memset(heap.free_blocks, 0, sizeof(heap.free_blocks));
	
	for (u32 i = 0; i < heap.max_allocation_count; i += 1) {
		heap.unused_block_indices[i] = heap.max_allocation_count - i - 1;
	}
	heap.unused_block_count = heap.max_allocation_count;
	
	PushNewPageFreeBlock(&heap, heap.reserved_size);
}
