#pragma once
#include "Basic.h"
#include "BasicString.h"
#include "BasicArray.h"

struct DirectoryChangeTracker;
struct FileIoQueue;

struct FileHandle {
	void* handle = nullptr;
};

enum struct OpenFileFlags : u32 {
	None  = 0u,
	Read  = 1u << 0,
	Write = 1u << 1,
	Async = 1u << 2,
};
ENUM_FLAGS_OPERATORS(OpenFileFlags);

enum struct IoOperationStatus : u8 {
	Free      = 0,
	InFlight  = 1,
	Succeeded = 2,
	Failed    = 3,
};


FileHandle SystemOpenFile(StackAllocator* alloc, String path, OpenFileFlags flags);
bool SystemCloseFile(FileHandle handle);

bool SystemWriteFile(FileHandle handle, const void* data, u64 size, u64 offset);
bool SystemReadFile(FileHandle handle, void* data, u64 size, u64 offset);
u64  SystemFileSize(FileHandle handle);
String SystemReadFileToString(StackAllocator* alloc, String path);

FileIoQueue* SystemCreateFileIoQueue(StackAllocator* alloc, u32 queue_size, u8* buffer, u64 buffer_size);
void SystemReleaseFileIoQueue(FileIoQueue* queue);
u64 CmdSystemReadFile(FileIoQueue* queue, FileHandle file, u64 buffer_offset, u64 size, u64 offset);
u32 SystemSubmitFileIoQueue(FileIoQueue* queue);
void SystemCheckIoCompletion(FileIoQueue* queue);
IoOperationStatus SystemConfirmIoCompletion(FileIoQueue* queue, u64 io_completion_index);


bool SystemCreateDirectory(StackAllocator* alloc, String path);


DirectoryChangeTracker* CreateDirectoryChangeTracker(StackAllocator* alloc, String directory_path);
ArrayView<String> ReadDirectoryChangeEvents(StackAllocator* alloc, DirectoryChangeTracker* tracker);
void ReleaseDirectoryChangeTracker(DirectoryChangeTracker* tracker);

void SystemWriteToConsole(String message);
void SystemWriteToConsoleV(StackAllocator* alloc, String format, ArrayView<StringFormatArgument> arguments);
template<typename ... Args> void SystemWriteToConsole(StackAllocator* alloc, String format, Args ... args) { FORMAT_PROC_BODY(SystemWriteToConsoleV, alloc, format); }
