#pragma once
#include "Basic.h"
#include "BasicMemory.h"

template<typename T>
struct ArrayView {
	T*  data  = nullptr;
	u64 count = 0;
	
	using ValueType = T;
	
	T& operator[] (u64 index) { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	const T& operator[] (u64 index) const { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	
	T* begin() { return data; }
	T* end() { return data + count; }
	
	const T* begin() const { return data; }
	const T* end() const { return data + count; }
};

template<typename T>
struct Array {
	T*  data  = nullptr;
	u64 count = 0;
	u64 capacity = 0;
	
	using ValueType = T;
	
	T& operator[] (u64 index) { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	const T& operator[] (u64 index) const { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	
	T* begin() { return data; }
	T* end() { return data + count; }
	
	const T* begin() const { return data; }
	const T* end() const { return data + count; }
	
	operator ArrayView<T> () { return { data, count }; }
};

template<typename T, u64 fixed_capacity>
struct FixedCapacityArray {
	T data[fixed_capacity] = {};
	u64 count = 0;
	compile_const u64 capacity = fixed_capacity;
	
	using ValueType = T;
	
	T& operator[] (u64 index) { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	const T& operator[] (u64 index) const { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	
	T* begin() { return data; }
	T* end() { return data + count; }
	
	const T* begin() const { return data; }
	const T* end() const { return data + count; }
	
	operator ArrayView<T> () { return { data, count }; }
};

template<typename T, u64 fixed_count>
struct FixedCountArray {
	T data[fixed_count] = {};
	compile_const u64 count    = fixed_count;
	compile_const u64 capacity = fixed_count;
	
	using ValueType = T;
	
	T& operator[] (u64 index) { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	const T& operator[] (u64 index) const { DebugAssert(index < count, "Array access out of bounds. %llu/%llu.", index, count); return data[index]; }
	
	T* begin() { return data; }
	T* end() { return data + count; }
	
	const T* begin() const { return data; }
	const T* end() const { return data + count; }
	
	operator ArrayView<T> () { return { data, count }; }
};


template<typename T, typename AllocatorT>
void ArrayReserve(Array<T>& array, AllocatorT* alloc, u64 new_capacity) {
	if (array.capacity >= new_capacity) return;
	array.data = (T*)alloc->Reallocate(array.data, array.capacity * sizeof(T), new_capacity * sizeof(T), alignof(T));
	array.capacity = new_capacity;
}

template<typename T, typename AllocatorT>
void ArrayResize(Array<T>& array, AllocatorT* alloc, u64 new_count) {
	ArrayReserve(array, alloc, new_count);
	for (u64 i = array.count; i < new_count; i += 1) {
		NewInPlace(array.data + i, T{});
	}
	array.count = new_count;
}

template<typename T, typename AllocatorT>
void ArrayResizeMemset(Array<T>& array, AllocatorT* alloc, u64 new_count, u8 pattern = 0) {
	ArrayReserve(array, alloc, new_count);
	if (new_count > array.count) memset(array.data + array.count, pattern, new_count - array.count);
	array.count = new_count;
}

template<typename T, typename AllocatorT>
void ArrayAppend(Array<T>& array, AllocatorT* alloc, const typename Array<T>::ValueType& value) {
	if (array.count >= array.capacity) ArrayReserve(array, alloc, array.capacity ? (array.capacity * 3 / 2 + 1) : 8);
	array[array.count++] = value;
}

template<typename T, typename AllocatorT>
T& ArrayEmplace(Array<T>& array, AllocatorT* alloc) {
	if (array.count >= array.capacity) ArrayReserve(array, alloc, array.capacity ? (array.capacity * 3 / 2 + 1) : 8);
	return array[array.count++] = {};
}


template<typename ArrayT>
void ArrayAppend(ArrayT& array, const typename ArrayT::ValueType& value) {
	DebugAssert(array.count < array.capacity, "ArrayAppend overflowed allocated buffer: %llu.", array.capacity);
	array[array.count++] = value;
}

template<typename ArrayT>
typename ArrayT::ValueType& ArrayEmplace(ArrayT& array) {
	DebugAssert(array.count < array.capacity, "ArrayAppend overflowed allocated buffer: %llu.", array.capacity);
	return array[array.count++] = {};
}


template<typename ArrayT>
void ArrayEraseSwapLast(ArrayT& array, u64 index) {
	if (index + 1 != array.count) array[index] = array[array.count - 1];
	array.count -= 1;
}

template<typename ArrayT>
void ArrayErase(ArrayT& array, u64 index) {
	if (index + 1 != array.count) memmove(&array[index], &array[index + 1], (array.count - index - 1) * sizeof(typename ArrayT::ValueType));
	array.count -= 1;
}

template<typename ArrayT> typename ArrayT::ValueType ArrayPopFirst(ArrayT& array) { auto result = array[0]; ArrayErase(array, 0); return result; }
template<typename ArrayT> typename ArrayT::ValueType ArrayPopLast(ArrayT& array) { auto result = array[array.count - 1]; array.count -= 1; return result; }
template<typename ArrayT> typename ArrayT::ValueType& ArrayFirstElement(ArrayT& array) { return array[0]; }
template<typename ArrayT> typename ArrayT::ValueType& ArrayLastElement(ArrayT& array) { return array[array.count - 1]; }

template<typename ArrayT, typename AllocatorT>
ArrayView<typename ArrayT::ValueType> ArrayCopy(ArrayT& array, AllocatorT* alloc) {
	using ValueType = typename ArrayT::ValueType;
	
	ArrayView<ValueType> result;
	result.data  = (ValueType*)alloc->Allocate(array.count * sizeof(ValueType), alignof(ValueType));
	result.count = array.count;
	memcpy(result.data, array.data, result.count * sizeof(ValueType));
	
	return result;
}

