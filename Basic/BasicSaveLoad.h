#pragma once
#include "Basic.h"
#include "BasicMath.h"
#include "BasicMemory.h"
#include "BasicString.h"
#include "BasicHashTable.h"

struct alignas(64) SaveLoadBuffer {
	compile_const u64 minimum_entry_size = 4 * 1024;
	
	StackAllocator* alloc = nullptr; // When saving, we rely on being able to grow data array from this allocator.
	HeapAllocator*  heap  = nullptr;
	Array<u8>       data  = {};
	
	SaveLoadDirection direction = SaveLoadDirection::None;
	
	String filepath;
	
	
	u8* ReserveSaveBytes(u64 size) {
		DebugAssert(direction == SaveLoadDirection::Saving, "Trying to write to SaveLoad buffer with save load direction '%'.", (u32)direction);
		
		if (data.count + size > data.capacity) {
			u64 new_capacity = Math::Max(data.capacity * 3 / 2 + 1, minimum_entry_size);
			data.data     = (u8*)alloc->Reallocate(data.data, data.capacity, new_capacity);
			data.capacity = new_capacity;
		}
		
		u8* result = data.data + data.count;
		data.count += size;
		
		return result;
	}
	
	u8* ReserveLoadBytes(u64 size) {
		DebugAssert(direction == SaveLoadDirection::Loading, "Trying to read from SaveLoad buffer with save load direction '%'.", (u32)direction);
		DebugAssert(data.count + size <= data.capacity, "SaveLoad buffer overflowed when loading %0 bytes. (%0 > %1).", size, data.capacity - data.count);
		
		u8* result = data.data + data.count;
		data.count += size;
		
		return result;
	}
	
	always_inline u8* ReserveSaveLoadBytes(u64 size) {
		u8* result = nullptr;
		if (direction == SaveLoadDirection::Loading) {
			result = ReserveLoadBytes(size);
		} else if (direction == SaveLoadDirection::Saving) {
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
		if (direction == SaveLoadDirection::Loading) {
			LoadBytes(data, size);
		} else if (direction == SaveLoadDirection::Saving) {
			SaveBytes(data, size);
		}
	}
};
static_assert(sizeof(SaveLoadBuffer) == 64, "Incorrect SaveLoad buffer size.");

bool OpenSaveLoadBuffer(StackAllocator* alloc, String filepath, SaveLoadDirection direction, SaveLoadBuffer& buffer);
bool CloseSaveLoadBuffer(SaveLoadBuffer& buffer);


template<typename T, typename ValueType = typename SaveLoadAsBytes<T>::ValueType>
always_inline void SaveLoad(SaveLoadBuffer& buffer, T& value, u64 version = 0) {
	buffer.SaveLoadBytes(&value, sizeof(value));
}

always_inline void SaveLoad(SaveLoadBuffer& buffer, String& value, u64 version = 0) {
	u64 count = value.count;
	SaveLoad(buffer, count);
	
	if (buffer.direction == SaveLoadDirection::Loading) {
		if (buffer.heap && value.data) {
			buffer.heap->Deallocate(value.data);
		}
		
		value = {};
		if (count && buffer.heap) {
			value = StringAllocate(buffer.heap, count);
			buffer.LoadBytes(value.data, count);
		} else if (count) {
			value = { (char*)buffer.ReserveLoadBytes(count), count };
		}
	} else if (buffer.direction == SaveLoadDirection::Saving) {
		buffer.SaveBytes(value.data, count);
	}
}

template<typename T>
void SaveLoad(SaveLoadBuffer& buffer, Array<T>& array, u64 version = 0) {
	u64 count = array.count;
	SaveLoad(buffer, count);
	
	if (buffer.direction == SaveLoadDirection::Loading) {
		if (buffer.heap && array.data) {
			buffer.heap->Deallocate(array.data);
		}
		
		array = {};
		if (count && buffer.heap) {
			ArrayResize(array, buffer.heap, count);
		} else if (count) {
			ArrayResize(array, buffer.alloc, count);
		}
		
		for (auto& value : array) {
			SaveLoad(buffer, value, version);
		}
	} else if (buffer.direction == SaveLoadDirection::Saving) {
		for (auto& value : array) {
			SaveLoad(buffer, value, version);
		}
	}
}

template<typename T>
void SaveLoad(SaveLoadBuffer& buffer, ArrayView<T>& array, u64 version = 0) {
	u64 count = array.count;
	SaveLoad(buffer, count);
	
	if (buffer.direction == SaveLoadDirection::Loading) {
		if (buffer.heap && array.data) {
			buffer.heap->Deallocate(array.data);
		}
		
		array = {};
		if (count && buffer.heap) {
			array = ArrayViewAllocate<T>(buffer.heap, count);
		} else if (count) {
			array = ArrayViewAllocate<T>(buffer.alloc, count);
		}
		
		for (auto& value : array) {
			SaveLoad(buffer, value, version);
		}
	} else if (buffer.direction == SaveLoadDirection::Saving) {
		for (auto& value : array) {
			SaveLoad(buffer, value, version);
		}
	}
}

template<typename KeyT, typename ValueT>
void SaveLoad(SaveLoadBuffer& buffer, HashTable<KeyT, ValueT>& hash_table, u64 version = 0) {
	u64 count = hash_table.count;
	SaveLoad(buffer, count);
	
	if (buffer.direction == SaveLoadDirection::Loading) {
		if (buffer.heap && hash_table.metadata) {
			HashTableDeallocate(hash_table, buffer.heap);
		}
		
		hash_table = {};
		if (count && buffer.heap) {
			HashTableReserve(hash_table, buffer.heap, count);
		} else if (count) {
			HashTableReserve(hash_table, buffer.alloc, count);
		}
		
		for (u64 i = 0; i < count; i += 1) {
			HashTableElement<KeyT, ValueT> element;
			SaveLoad(buffer, element, version);
			HashTableAddOrFindElement(hash_table, element);
		}
	} else if (buffer.direction == SaveLoadDirection::Saving) {
		for (auto& element : hash_table) {
			SaveLoad(buffer, element, version);
		}
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
