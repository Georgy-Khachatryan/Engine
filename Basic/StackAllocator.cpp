#include "BasicMemory.h"
#include "BasicMath.h"

struct StackAllocatorBlock {
	StackAllocatorBlock* last_block = nullptr;
	u64 reserved_size  = 0;
	u64 committed_size = 0;
	u64 allocated_size = 0;
};

compile_const u64 allocation_granularity = 64 * 1024;

static StackAllocatorBlock* AllocateNewBlock(u64 reserved_size, u64 committed_size, StackAllocatorBlock* last_block) {
	reserved_size  = reserved_size  < allocation_granularity ? allocation_granularity : AlignUp(reserved_size,  allocation_granularity);
	committed_size = committed_size < allocation_granularity ? allocation_granularity : AlignUp(committed_size, allocation_granularity);
	
	auto* memory = SystemAllocateAddressSpace(reserved_size);
	DebugAssert(memory != nullptr, "Failed to reserve virtual address range.");
	
	bool success = SystemCommitMemoryPages(memory, committed_size);
	DebugAssert(success, "Failed to commit memory pages.");
	
	auto* block = NewInPlace(memory, StackAllocatorBlock);
	block->last_block     = last_block;
	block->allocated_size = sizeof(StackAllocatorBlock);
	block->committed_size = committed_size;
	block->reserved_size  = reserved_size;
	
	return block;
}

void* StackAllocator::Allocate(u64 size, u64 alignment) {
	if (size == 0) return nullptr;
	
	auto* block = current_block;
	
	// We assume that we only need to align allocation_offset and the base address is already aligned.
	DebugAssert(alignment <= allocation_granularity, "Alignment is too high (%llu/%llu).", alignment, allocation_granularity);
	
	u64 allocation_offset = AlignUp(block->allocated_size, alignment);
	if (allocation_offset + size > block->reserved_size) {
		u64 new_block_committed_size = size + AlignUp(sizeof(StackAllocatorBlock), alignment);
		u64 new_block_reserved_size  = block->reserved_size > new_block_committed_size ? block->reserved_size : new_block_committed_size;
		
		block = AllocateNewBlock(new_block_reserved_size, new_block_committed_size, block);
		allocation_offset = AlignUp(block->allocated_size, alignment);
		
		current_block = block;
	}
	
	if (allocation_offset + size > block->committed_size) {
		u64 old_committed_size = block->committed_size;
		u64 new_committed_size = AlignUp(allocation_offset + size, allocation_granularity);
		
		bool success = SystemCommitMemoryPages((u8*)block + old_committed_size, new_committed_size - old_committed_size);
		DebugAssert(success, "Failed to commit memory pages.");
		
		block->committed_size = new_committed_size;
	}
	
	// Make sure to count padding towards to allocated memory amount.
	u64 size_with_padding = allocation_offset + size - block->allocated_size;
	total_allocated_size += size_with_padding;
	
	void* result = (u8*)block + allocation_offset;
	block->allocated_size = allocation_offset + size;
	
	return result;
}

void* StackAllocator::Reallocate(void* old_memory, u64 old_size, u64 new_size, u64 alignment) {
	if (old_memory == nullptr) return Allocate(new_size, alignment);
	if (new_size == 0)         return Deallocate(old_memory, old_size), nullptr;
	if (new_size <= old_size)  return old_memory;
	
	auto* block = current_block;
	bool can_reallocate =
		(((u8*)old_memory + old_size) == ((u8*)block + block->allocated_size)) &&
		(new_size - old_size) < (block->reserved_size - block->allocated_size);
	
	if (can_reallocate) {
		block->allocated_size -= old_size;
		total_allocated_size  -= old_size;
	}
	
	void* result = Allocate(new_size, alignment);
	if (can_reallocate == false) memcpy(result, old_memory, old_size);
	
	return result;
}

void StackAllocator::Deallocate(void* old_memory, u64 old_size) {
	if (old_memory == nullptr) return;
	
	auto* block = current_block;
	
	bool can_deallocate = ((u8*)old_memory + old_size) == ((u8*)block + block->allocated_size);
	if (can_deallocate) {
		block->allocated_size -= old_size;
		total_allocated_size  -= old_size;
	}
}

void StackAllocator::DeallocateToSize(u64 new_size) {
	auto* block = current_block;
	
	DebugAssert(total_allocated_size >= new_size, "Trying to deallocate already deallocated memory (%llu/%llu).", new_size, total_allocated_size);
	u64 size_to_deallocate = total_allocated_size - new_size;
	
	while (size_to_deallocate != 0) {
		// Header is not a part of the total_allocated_size, but it is a part of allocated_size.
		u64 block_total_allocated_size = (block->allocated_size - sizeof(StackAllocatorBlock));
		
		if (size_to_deallocate > block_total_allocated_size) {
			size_to_deallocate -= block_total_allocated_size;
			
			auto* last_block = block->last_block;
			
			bool success = SystemDeallocateAddressSpace(block);
			DebugAssert(success, "Failed to free virtual address range.");
			
			block = last_block;
		} else {
			block->allocated_size -= size_to_deallocate;
			size_to_deallocate = 0;
		}
	}
	DebugAssert(block != nullptr, "Allocator is empty after DeallocateToSize(%llu).", new_size);
	
	current_block        = block;
	total_allocated_size = new_size;
}


StackAllocator CreateStackAllocator(u64 reserved_size, u64 committed_size) {
	auto* block = AllocateNewBlock(reserved_size, committed_size, nullptr);
	
	StackAllocator alloc;
	alloc.current_block        = block;
	alloc.total_allocated_size = 0;
	
	return alloc;
}

void ReleaseStackAllocator(StackAllocator& alloc) {
	auto* block = alloc.current_block;
	alloc.current_block        = nullptr;
	alloc.total_allocated_size = 0;
	
	while (block != nullptr) {
		auto* last_block = block->last_block;
		
		bool success = SystemDeallocateAddressSpace(block);
		DebugAssert(success, "Failed to free virtual address range.");
		
		block = last_block;
	}
}
