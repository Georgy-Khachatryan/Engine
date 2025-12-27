#pragma once
#include "Basic.h"
#include "BasicString.h"
#include "BasicArray.h"

struct FileHandle {
	void* handle = nullptr;
};

enum struct OpenFileFlags : u32 {
	None  = 0u,
	Read  = 1u << 0,
	Write = 1u << 1,
};
ENUM_FLAGS_OPERATORS(OpenFileFlags);

FileHandle SystemOpenFile(StackAllocator* alloc, String path, OpenFileFlags flags);
void SystemCloseFile(FileHandle handle);

bool SystemWriteFile(FileHandle handle, const void* data, u64 size, u64 offset);
bool SystemReadFile(FileHandle handle, void* data, u64 size, u64 offset);
u64  SystemFileSize(FileHandle handle);

String SystemReadFileToString(StackAllocator* alloc, String path);

bool SystemCreateDirectory(StackAllocator* alloc, String path);

struct DirectoryChangeTracker;

DirectoryChangeTracker* CreateDirectoryChangeTracker(StackAllocator* alloc, String directory_path);
ArrayView<String> ReadDirectoryChangeEvents(StackAllocator* alloc, DirectoryChangeTracker* tracker);
void ReleaseDirectoryChangeTracker(DirectoryChangeTracker* tracker);

void SystemWriteToConsole(String message);
void SystemWriteToConsoleV(StackAllocator* alloc, String format, ArrayView<StringFormatArgument> arguments);
template<typename ... Args> void SystemWriteToConsole(StackAllocator* alloc, String format, Args ... args) { FORMAT_PROC_BODY(SystemWriteToConsoleV, alloc, format); }
