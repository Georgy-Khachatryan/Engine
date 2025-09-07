#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"

#include <stdio.h>

s32 main() {
	auto alloc = CreateStackAllocator(256 * 1024, 64 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	{
		TempAllocationScope(&alloc);
		DebugAssert(alloc.total_allocated_size == 0, "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
		void* b0 = alloc.Allocate(256, 256);
		DebugAssert(alloc.total_allocated_size == 480, "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	}
	
	{
		TempAllocationScope(&alloc);
		DebugAssert(alloc.total_allocated_size == 0, "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
		void* b0 = alloc.Allocate(256, 256);
		DebugAssert(alloc.total_allocated_size == 480, "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	}
	
	void* b0 = alloc.Allocate(64 * 1024);
	memset(b0, 0xB0, 64 * 1024);
	DebugAssert(alloc.total_allocated_size == (64 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	
	void* b1 = alloc.Allocate(128 * 1024, 256);
	memset(b1, 0xB1, 128 * 1024);
	DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	
	u64 total_allocated_size = alloc.total_allocated_size;
	
	void* b2 = alloc.Allocate(64 * 1024);
	DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024 + 64 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	memset(b2, 0xB2, 64 * 1024);
	
	alloc.DeallocateToSize(total_allocated_size);
	DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	
	{
		TempAllocationScope(&alloc);
		
		void* b3 = alloc.Allocate(64 * 1024);
		memset(b3, 0xB3, 64 * 1024);
		DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024 + 64 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	}
	
	{
		TempAllocationScopeNamed(size_b3, &alloc);
		
		void* b3 = alloc.Allocate(64 * 1024);
		memset(b3, 0xB3, 64 * 1024);
		DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024 + 64 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	}
	
	{
		TempAllocationScope(&alloc);
		TempAllocationScope(&alloc);
	
		void* b3 = alloc.Allocate(64 * 1024);
		memset(b3, 0xB3, 64 * 1024);
		DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024 + 64 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	}
	
	void* b4 = alloc.Allocate(64 * 1024);
	memset(b4, 0xB4, 64 * 1024);
	DebugAssert(alloc.total_allocated_size == (224 + 64 * 1024 + 128 * 1024 + 64 * 1024), "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	
	alloc.DeallocateToSize(0);
	DebugAssert(alloc.total_allocated_size == 0, "Invalid total_allocated_size: %llu.", alloc.total_allocated_size);
	
	
	static_assert(sizeof(s8) == 1);
	static_assert(sizeof(u8) == 1);

	static_assert(sizeof(s16) == 2);
	static_assert(sizeof(u16) == 2);

	static_assert(sizeof(s32) == 4);
	static_assert(sizeof(u32) == 4);

	static_assert(sizeof(s64) == 8);
	static_assert(sizeof(u64) == 8);

#if 0
	for (u32 i = 0; i < 2; i += 1) {
		DebugAssert(false, "Assert from %s", __FUNCTION__);
		DebugAssertOnce(false, "AssertOnce from %s", __FUNCTION__);
		DebugAssertAlways("AssertAlways from %s", __FUNCTION__);
	}
#endif

	defer{ printf("Deferred\n"); };
	printf("Regular\n");
}
