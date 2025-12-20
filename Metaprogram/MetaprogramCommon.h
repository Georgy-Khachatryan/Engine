#pragma once
#include "Basic/Basic.h"
#include "TypeInfo.h"

template<typename T>
inline T* TypeInfoCast(TypeInfo* type_info) {
	return type_info && type_info->info_type == T::my_type ? (T*)type_info : nullptr;
}

template<typename T>
inline T* FindNote(ArrayView<TypeInfoNote> notes) {
	auto* type_info = TypeInfoOf<T>();
	if (type_info == nullptr) return nullptr;
	
	for (auto& note : notes) {
		if (note.type == type_info) return (T*)note.value;
	}
	return nullptr;
}

template<typename T>
inline T* FindNote(TypeInfoStruct* type_info) { return FindNote<T>(type_info->notes); }

template<typename T>
inline T* FindNote(TypeInfo* type_info) {
	if (type_info == nullptr) return nullptr;
	
	switch (type_info->info_type) {
	case TypeInfoType::Struct: return FindNote<T>(static_cast<TypeInfoStruct*>(type_info)->notes);
	case TypeInfoType::Enum:   return FindNote<T>(static_cast<TypeInfoEnum*>(type_info)->notes);
	default: return nullptr;
	}
}


TypeInfo* ExtractTemplateParameterType(TypeInfo* type_info, u32 index);
String ExtractNameWithoutNamespace(String name);

u64 ComputeTypeSize(TypeInfo* type_info);
u64 ReadIntegerU64(TypeInfoInteger* type_info, const void* value);

String PrintTypeName(StackAllocator* alloc, TypeInfo* type_info);
String PrintTypeValue(StackAllocator* alloc, TypeInfo* type_info, const void* value);


void WriteGeneratedFile(StackAllocator* alloc, String filepath, String contents);
void EnsureDirectoryExists(StackAllocator* alloc, String directory);

