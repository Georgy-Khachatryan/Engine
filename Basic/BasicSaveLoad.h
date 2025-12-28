#pragma once
#include "Basic.h"
#include "BasicMath.h"
#include "BasicMemory.h"
#include "BasicString.h"
#include "BasicHashTable.h"

struct alignas(64) SaveLoadBuffer {
	compile_const u64 minimum_entry_size = 4 * 1024;
	
	StackAllocator* alloc = nullptr;
	HeapAllocator*  heap  = nullptr;
	
	u8* cursor = nullptr;
	u32 remaining_size = 0;
	u32 global_offset  = 0;
	Array<ArrayView<u8>> chunks;
	
	bool is_saving  = false;
	bool is_loading = false;
	
	u8* ReserveSaveBytes(u64 size) {
		DebugAssert(is_saving, "Trying to write to SaveLoad buffer with no write flag.");
		
		if (size > remaining_size) {
			if (remaining_size != 0) {
				auto& last_chunk = ArrayLastElement(chunks);
				last_chunk.count -= remaining_size;
			}
			
			u64 new_chunk_capacity = AlignUp(Max(minimum_entry_size, size), minimum_entry_size);
			
			cursor         = (u8*)alloc->Allocate(new_chunk_capacity);
			remaining_size = (u32)new_chunk_capacity;
			
			ArrayAppend(chunks, alloc, { cursor, new_chunk_capacity });
		}
		
		u8* result = cursor;
		cursor         += (u32)size;
		remaining_size -= (u32)size;
		global_offset  += (u32)size;
		
		return result;
	}
	
	u8* ReserveLoadBytes(u64 size) {
		DebugAssert(is_loading, "Trying to read from SaveLoad buffer with no read flag.");
		DebugAssert(size <= remaining_size, "SaveLoad buffer overflowed when loading %0 bytes. (%0 > %1).", size, remaining_size);
		
		u8* result = cursor;
		cursor         += (u32)size;
		remaining_size -= (u32)size;
		global_offset  += (u32)size;
		
		return result;
	}
	
	always_inline u8* ReserveSaveLoadBytes(u64 size) {
		u8* result = nullptr;
		if (is_loading) {
			result = ReserveLoadBytes(size);
		} else if (is_saving) {
			result = ReserveSaveBytes(size);
		}
		return result;
	}
	
	always_inline void SaveBytes(const void* data, u64 size) {
		memcpy(ReserveSaveBytes(size), data, size);
	}
	
	always_inline void LoadBytes(void* data, u64 size) {
		memcpy(data, ReserveLoadBytes(size), size);
	}
	
	always_inline void SaveLoadBytes(void* data, u64 size) {
		if (is_loading) {
			LoadBytes(data, size);
		} else if (is_saving) {
			SaveBytes(data, size);
		}
	}
};
static_assert(sizeof(SaveLoadBuffer) == 64, "Incorrect SaveLoad buffer size.");

SaveLoadBuffer OpenSaveLoadBufferForSaving(StackAllocator* alloc);
bool WriteSaveLoadBufferToFile(StackAllocator* alloc, SaveLoadBuffer& buffer, String path);
bool OpenSaveLoadBufferForLoading(StackAllocator* alloc, String path, SaveLoadBuffer& buffer);


template<typename T, typename ValueType = typename SaveLoadAsBytes<T>::ValueType>
always_inline void SaveLoad(SaveLoadBuffer& buffer, T& value, u64 version = 0) {
	buffer.SaveLoadBytes(&value, sizeof(value));
}

always_inline void SaveLoad(SaveLoadBuffer& buffer, String& value, u64 version = 0) {
	u64 count = value.count;
	SaveLoad(buffer, count);
	
	if (buffer.is_loading) {
		value = count && buffer.heap ? StringAllocate(buffer.heap, count) : String{};
		buffer.LoadBytes(value.data, count);
	} else if (buffer.is_saving) {
		buffer.SaveBytes(value.data, count);
	}
}

template<typename T>
always_inline void SaveLoadDummy(SaveLoadBuffer& buffer, u64 version = 0) {
	// Don't allow heap allocations since we're not going to keep results.
	auto* old_heap = buffer.heap;
	buffer.heap = nullptr;
	
	T dummy;
	SaveLoad(buffer, dummy, version);
	
	buffer.heap = old_heap;
}

using SaveLoadCallback = void (*)(SaveLoadBuffer& buffer, void* data, u64 version);
