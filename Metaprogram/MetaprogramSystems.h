#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicString.h"
#include "TypeInfo.h"

struct TypeInfo;
struct TypeInfoStruct;
struct TypeInfoEnum;
struct VersionedTypeInfo;

HashTable<String, VersionedTypeInfo> ParseSaveLoadVersionHistory(StackAllocator* alloc);
void WriteSaveLoadVersionHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo> version_history);
void WriteSaveLoadCallbacks(StackAllocator* alloc, HashTable<String, VersionedTypeInfo> version_history);
u64 AddTypeInfoToSaveLoadHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo>& version_history, TypeInfo* type_info);

void WriteEntitySystemMetadata(StackAllocator* alloc, ArrayView<TypeInfoStruct*> entity_type_infos, ArrayView<TypeInfoStruct*> entity_query_type_infos, HashTable<String, VersionedTypeInfo>& version_history);

void WriteCodeForShaderDefinitions(StackAllocator* alloc, ArrayView<TypeInfoEnum*> shader_definition_type_infos);
void WriteCodeForRenderPasses(StackAllocator* alloc, ArrayView<TypeInfoStruct*> hlsl_file_type_infos, ArrayView<TypeInfoStruct*> render_pass_type_infos);

void WriteCodeForMathLibrary(StackAllocator* alloc);


void ReportErrorV(StackAllocator* alloc, u64 source_location, String format, ArrayView<StringFormatArgument> arguments);
template<typename ... Args>
void ReportError(StackAllocator* alloc, u64 source_location, String format, Args ... args) { FORMAT_PROC_BODY(ReportErrorV, alloc, source_location, format); }
void CheckFieldIsReflected(StackAllocator* alloc, TypeInfoStruct* type_info, TypeInfoStructField& field);
