#pragma once
#include "Basic.h"

void* SystemAllocateAddressSpace(u64 reserved_size);
bool SystemDeallocateAddressSpace(void* address);
bool SystemCommitMemoryPages(void* address, u64 committed_size);

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

#define TempAllocationScopeNamed(name, alloc) u64 name = (alloc)->total_allocated_size; defer{ (alloc)->DeallocateToSize(name); }
#define TempAllocationScope(alloc) TempAllocationScopeNamed(CREATE_UNIQUE_NAME(temp_allocated_size_), alloc)

inline void* operator new(u64 size, StackAllocator* alloc, u64 alignment) noexcept { return alloc->Allocate(size, alignment); }
#define NewFromAlloc(alloc, type) new (alloc, alignof(type)) type

