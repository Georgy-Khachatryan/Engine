#pragma once
#include "Basic.h"

void* SystemAllocateAddressSpace(u64 reserved_size);
bool SystemDeallocateAddressSpace(void* address);
bool SystemCommitMemoryPages(void* address, u64 committed_size);

template<typename T> struct Array;

struct StackAllocatorBlock;

struct StackAllocator {
	StackAllocatorBlock* current_block = nullptr;
	u64 total_allocated_size = 0;
	
	void* Allocate(u64 size, u64 alignment = 8);
	void* Reallocate(void* old_memory, u64 old_size, u64 new_size, u64 alignment = 8);
	void  Deallocate(void* old_memory, u64 old_size);
	
	void DeallocateToSize(u64 new_size);
};

StackAllocator CreateStackAllocator(u64 reserved_size, u64 committed_size);
void ReleaseStackAllocator(StackAllocator& alloc);


template<typename BlockTypeT>
struct HeapAllocatorBase {
	using BlockType = BlockTypeT;
	
	u32 mask_level_0 = 0;
	u8  mask_level_1[32] = {};
	
	u32 reserved_size = 0;
	BlockTypeT* free_blocks[208] = {}; // 208 = ComputeBinIndex(maximum_size, true);
};


struct HeapAllocatorBlock;
struct HeapAllocatorPage;

struct HeapAllocator : HeapAllocatorBase<HeapAllocatorBlock> {
	HeapAllocatorPage* current_page = nullptr;
	
	void* Allocate(u64 size, u64 alignment = 8);
	void* Reallocate(void* old_memory, u64 old_size, u64 new_size, u64 alignment = 8);
	void  Deallocate(void* old_memory, u64 old_size = 0);
	u64 GetMemoryBlockSize(void* memory);
	
	u64 ComputeTotalMemoryUsage();
};

HeapAllocator CreateHeapAllocator(u64 reserved_size);
void ReleaseHeapAllocator(HeapAllocator& alloc);
void ResetHeapAllocator(HeapAllocator& alloc);


struct NumaHeapAllocatorBlock;
struct NumaHeapAllocation { u32 index = u32_max; };

struct NumaMemoryMoveCommand {
	NumaHeapAllocation allocation;
	u32 old_offset = 0;
	u32 new_offset = 0;
	u32 size       = 0;
};

struct NumaHeapAllocator : HeapAllocatorBase<NumaHeapAllocatorBlock> {
	NumaHeapAllocatorBlock* blocks = nullptr;
	u32* unused_block_indices = nullptr;
	u32 unused_block_count = 0;
	u32 max_allocation_count = 0;
	
	NumaHeapAllocation Allocate(u64 size);
	void ReallocateShrink(NumaHeapAllocation allocation, u64 new_size);
	void Deallocate(NumaHeapAllocation allocation);
	
	void CompactMemoryBlocks(StackAllocator* alloc, Array<NumaMemoryMoveCommand>& move_commands);
	float ComputeFragmentation();
	
	u64 GetMemoryBlockOffset(NumaHeapAllocation allocation);
	u64 GetMemoryBlockSize(NumaHeapAllocation allocation);
};

NumaHeapAllocator CreateNumaHeapAllocator(StackAllocator* alloc, u32 max_allocation_count, u32 reserved_size);
void ResetNumaHeapAllocator(NumaHeapAllocator& heap);


#define TempAllocationScopeNamed(name, alloc) u64 name = (alloc)->total_allocated_size; defer{ (alloc)->DeallocateToSize(name); }
#define TempAllocationScope(alloc) TempAllocationScopeNamed(CREATE_UNIQUE_NAME(temp_allocated_size_), alloc)

inline void* operator new(u64 size, StackAllocator* alloc, u64 alignment) noexcept { return alloc->Allocate(size, alignment); }
inline void* operator new(u64 size, HeapAllocator* alloc, u64 alignment) noexcept { return alloc->Allocate(size, alignment); }
#define NewFromAlloc(alloc, type) new (alloc, alignof(type)) type

enum struct NewInPlaceToken : u32 {};
inline void* operator new(u64 size, void* memory, NewInPlaceToken) { return memory; }
#define NewInPlace(memory, type) new (memory, NewInPlaceToken{}) type

