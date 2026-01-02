#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicString.h"

void BasicExamples(StackAllocator* alloc) {
#if ENABLE_FEATURE(ASSERTS)
	// Stack allocator:
	{
		TempAllocationScopeNamed(initial_size, alloc);
		
		void* memory0 = alloc->Allocate(32, 32);
		void* memory1 = alloc->Reallocate(memory0, 32, 128, 32);
		DebugAssert(memory0 == memory1, "Reallocation failed.");
		
		void* memory2 = alloc->Allocate(64);
		void* memory3 = alloc->Reallocate(memory0, 128, 1024, 32);
		DebugAssert(memory0 != memory3, "Reallocation failed.");
		
		alloc->Deallocate(memory3, 1024);
		alloc->Deallocate(memory2, 64);
		alloc->Deallocate(memory1, 128);
		DebugAssert(alloc->total_allocated_size == initial_size, "Deallocation failed.");
	}
	
	// String formatting:
	{
		TempAllocationScope(alloc);
		
		auto string0 = StringFormat(alloc, "Number: %.."_sl, 10u);
		DebugAssert(string0 == "Number: 10."_sl, "String is incorrectly formatted.");
		
		auto string1 = StringFormat(alloc, "%.%x"_sl, 10u, (s16)0xFFFF);
		DebugAssert(string1 == "10FFFF"_sl, "String is incorrectly formatted.");
		
		auto string2 = StringFormat(alloc, "% %"_sl, "PI is:", 3.14f);
		DebugAssert(string2 == "PI is: 3.14"_sl, "String is incorrectly formatted.");
	}
	
	// String utf8 <-> utf16 conversion:
	{	
		TempAllocationScope(alloc);
		
		auto utf8_source     = u8"Orange կատու."_sl;
		auto utf16_converted = StringUtf8ToUtf16(alloc, utf8_source);
		auto utf8_converted  = StringUtf16ToUtf8(alloc, utf16_converted);
		
		DebugAssert(utf8_source == utf8_converted, "String mismatch after utf8 -> utf16 -> utf8 conversion.");
	}
	
	// Resizable array:
	{
		TempAllocationScope(alloc);
		
		Array<u32> values;
		ArrayReserve(values, alloc, 10);
		
		for (u32 i = 0; i < 10; i += 1) {
			ArrayAppend(values, i);
		}
		
		for (u32 i = 10; i < 20; i += 1) {
			ArrayAppend(values, alloc, i);
		}
		
		u32 first = ArrayPopFirst(values);
		DebugAssert(first == 0, "Incorrect first value.");
		
		u32 last = ArrayPopLast(values);
		DebugAssert(last == 19, "Incorrect last value.");
		
		ArrayEraseSwapLast(values, 2);
		DebugAssert(values[2] == 18, "Erase swap last is incorrect.");
		
		ArrayEmplace(values) = 100;
		
		ArrayErase(values, 1);
		DebugAssert(values[1] == 18, "Erase is incorrect.");
	}
	
	// Fixed capacity array:
	{
		FixedCapacityArray<u32, 10> values;
		
		for (u32 i = 0; i < 10; i += 1) {
			ArrayAppend(values, i);
		}
		
		u32 first = ArrayPopFirst(values);
		DebugAssert(first == 0, "Incorrect first value.");
		
		u32 last = ArrayPopLast(values);
		DebugAssert(last == 9, "Incorrect last value.");
		
		ArrayEraseSwapLast(values, 2);
		DebugAssert(values[2] == 8, "Erase swap last is incorrect.");
		
		ArrayEmplace(values) = 100;
		
		ArrayErase(values, 1);
		DebugAssert(values[1] == 8, "Erase is incorrect.");
	}
	
	// Fixed count array:
	{
		FixedCountArray<u32, 10> values;
		
		for (u32 i = 0; i < 10; i += 1) {
			values[i] = i;
		}
		
		u32 first = ArrayFirstElement(values);
		DebugAssert(first == 0, "Incorrect first value.");
		
		u32 last = ArrayLastElement(values);
		DebugAssert(last == 9, "Incorrect last value.");
	}
	
	{
		FixedCountArray<u32, 5> values;
		values[0] = 10;
		values[1] = 4;
		values[2] = 19;
		values[3] = 7;
		values[4] = 8;
		
		HeapSort(ArrayView<u32>(values));
		
		DebugAssert(values[0] == 4, "Array is incorrectly sorted.");
		DebugAssert(values[1] == 7, "Array is incorrectly sorted.");
		DebugAssert(values[2] == 8, "Array is incorrectly sorted.");
		DebugAssert(values[3] == 10, "Array is incorrectly sorted.");
		DebugAssert(values[4] == 19, "Array is incorrectly sorted.");
	}
	
	{
		auto heap = CreateHeapAllocator(64 * 1024);
		defer{ ReleaseHeapAllocator(heap); };
		
		{
			auto* memory0 = heap.Allocate(32 * 1024 - 8);
			auto* memory1 = heap.Allocate(30720 - 8);
			
			heap.Deallocate(memory0);
			heap.Deallocate(memory1);
		}
		
		{
			auto* memory0 = heap.Allocate(128 * 1024 - 16);
			heap.Deallocate(memory0);
		}
		
		{
			auto* memory0 = heap.Allocate(1024);
			auto* memory1 = heap.Allocate(2048);
			auto* memory2 = heap.Allocate(4096);
			auto* memory3 = heap.Allocate(8192);
			auto* memory4 = heap.Allocate(16384);
			
			heap.Deallocate(memory1);
			heap.Deallocate(memory3);
			heap.Deallocate(memory2);
			heap.Deallocate(memory0);
			heap.Deallocate(memory4);
		}
		
		{
			auto* memory0 = heap.Allocate(1024);
			auto* memory1 = heap.Allocate(1024);
			auto* memory2 = heap.Allocate(1024);
			auto* memory3 = heap.Allocate(1024);
			auto* memory4 = heap.Allocate(1024);
			
			heap.Deallocate(memory1);
			heap.Deallocate(memory3);
			heap.Deallocate(memory2);
			heap.Deallocate(memory0);
			heap.Deallocate(memory4);
		}
		
		{
			auto* memory0 = heap.Allocate(1024);
			auto* memory1 = heap.Allocate(1024);
			auto* memory2 = heap.Allocate(1024);
			auto* memory3 = heap.Allocate(1024);
			auto* memory4 = heap.Allocate(1024);
			
			heap.Deallocate(memory1);
			heap.Deallocate(memory3);
			memory2 = heap.Reallocate(memory2, 1024, 1024 * 3);
			DebugAssert(memory2 == memory1, "Reallocation failed.");
			heap.Deallocate(memory0);
			heap.Deallocate(memory4);
			heap.Deallocate(memory2);
		}
		
		{
			auto* memory0 = heap.Allocate(128, 64);
			auto* memory1 = heap.Allocate(128, 64);
			auto* memory2 = heap.Allocate(128, 64);
			
			DebugAssert((u64)memory0 % 64 == 0, "Incorrect heap allocation alignment. Offset: %..", (u64)memory0 % 64);
			DebugAssert((u64)memory1 % 64 == 0, "Incorrect heap allocation alignment. Offset: %..", (u64)memory1 % 64);
			DebugAssert((u64)memory2 % 64 == 0, "Incorrect heap allocation alignment. Offset: %..", (u64)memory2 % 64);
			
			memset(memory0, 0xA0, 128);
			memset(memory1, 0xA1, 128);
			memset(memory2, 0xA2, 128);
			
			heap.Deallocate(memory0);
			memory1 = heap.Reallocate(memory1, 128, 192, 64);
			DebugAssert(memory1 == memory0, "Reallocation failed.");
			DebugAssert((u64)memory1 % 64 == 0, "Incorrect heap allocation alignment. Offset: %..", (u64)memory1 % 64);
			
			for (u32 i = 0; i < 128; i += 1) {
				u32 byte = ((u8*)memory1)[i];
				DebugAssert(byte == 0xA1, "Reallocation copy failed. 0x%x != 0xA1.", byte);
			}
		}
		
		{
			Array<u32> values;
			ArrayReserve(values, &heap, 10);
			defer{ heap.Deallocate(values.data); };
			
			for (u32 i = 0; i < 10; i += 1) {
				ArrayAppend(values, i);
			}
			
			for (u32 i = 10; i < 20; i += 1) {
				ArrayAppend(values, &heap, i);
			}
			
			u32 first = ArrayPopFirst(values);
			DebugAssert(first == 0, "Incorrect first value.");
			
			u32 last = ArrayPopLast(values);
			DebugAssert(last == 19, "Incorrect last value.");
			
			ArrayEraseSwapLast(values, 2);
			DebugAssert(values[2] == 18, "Erase swap last is incorrect.");
			
			ArrayEmplace(values) = 100;
			
			ArrayErase(values, 1);
			DebugAssert(values[1] == 18, "Erase is incorrect.");
		}
	}
	
	{
		auto heap = CreateHeapAllocator(64 * 1024);
		defer{ ReleaseHeapAllocator(heap); };
		
		{
			HashTable<u64, u64> hash_table;
			HashTableReserve(hash_table, &heap, 128);
			defer{ HashTableDeallocate(hash_table, &heap); };
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, i, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, i)->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, i, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, i)->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, i);
			}
		}
		
		{
			HashTable<String, u64> hash_table;
			HashTableReserve(hash_table, &heap, 128);
			defer{ HashTableDeallocate(hash_table, &heap); };
			
			TempAllocationScope(alloc);
			
			Array<String> keys;
			ArrayResize(keys, alloc, 128);
			
			for (u64 i = 0; i < 128; i += 1) {
				keys[i] = StringFormat(alloc, "Key: %"_sl, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, keys[i], i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, keys[i])->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, keys[i]);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, keys[i], i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, keys[i])->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, keys[i]);
			}
		}
	}
#endif // ENABLE_FEATURE(ASSERTS)
}
