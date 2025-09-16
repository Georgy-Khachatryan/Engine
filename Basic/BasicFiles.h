#pragma once
#include "Basic.h"
#include "BasicString.h"

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

void SystemWriteToConsole(StackAllocator* alloc, String message);
