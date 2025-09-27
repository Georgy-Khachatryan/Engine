#include "Basic.h"
#include "BasicArray.h"
#include "BasicMemory.h"
#include "BasicFiles.h"
#include "BasicString.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void* SystemAllocateAddressSpace(u64 reserved_size) {
	return VirtualAlloc(nullptr, reserved_size, MEM_RESERVE, PAGE_READWRITE);
}

bool SystemDeallocateAddressSpace(void* address) {
	return VirtualFree(address, 0, MEM_RELEASE) != 0;
}

bool SystemCommitMemoryPages(void* address, u64 committed_size) {
	return VirtualAlloc(address, committed_size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}


StringUtf16 StringUtf8ToUtf16(StackAllocator* alloc, StringUtf8 string) {
	StringUtf16 result = {};
	result.count = MultiByteToWideChar(CP_UTF8, 0, string.data, (s32)string.count, nullptr, 0);
	result.data  = (u16*)alloc->Allocate((result.count + 1) * sizeof(u16));
	
	MultiByteToWideChar(CP_UTF8, 0, string.data, (s32)string.count, (wchar_t*)result.data, (s32)result.count);
	result.data[result.count] = 0;
	
	return result;
}

StringUtf8 StringUtf16ToUtf8(StackAllocator* alloc, StringUtf16 string) {
	StringUtf8 result = {};
	result.count = WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)string.data, (s32)string.count, nullptr, 0, 0, 0);
	result.data  = (char*)alloc->Allocate(result.count + 1);
	
	WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)string.data, (s32)string.count, result.data, (s32)result.count, 0, 0);
	result.data[result.count] = 0;
	
	return result;
}


FileHandle SystemOpenFile(StackAllocator* alloc, String path, OpenFileFlags flags) {
	TempAllocationScope(alloc);
	
	DWORD access        = 0;
	DWORD sharing_mode  = 0;
	DWORD creation_mode = 0;
	DWORD attributes    = 0;
	
	//
	// - Don't support simultaneous read and write.
	// - For read open an existing file. Allow sharing with other processes for reading and writing.
	// - For write always create a new file. Don't allow shared reading or writing.
	//
	switch (flags & (OpenFileFlags::Read | OpenFileFlags::Write)) {
	case OpenFileFlags::Read:  access = GENERIC_READ;  creation_mode = OPEN_EXISTING; sharing_mode = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
	case OpenFileFlags::Write: access = GENERIC_WRITE; creation_mode = CREATE_ALWAYS; break;
	default: DebugAssertAlways("Invalid combination of open file flags '%u'.", (u32)flags);
	}
	
	auto path_utf16 = StringUtf8ToUtf16(alloc, path);
	
	auto handle = CreateFileW((wchar_t*)path_utf16.data, access, sharing_mode, nullptr, creation_mode, attributes, nullptr);
	
	return FileHandle{ handle == INVALID_HANDLE_VALUE ? nullptr : handle };
}

void SystemCloseFile(FileHandle handle) {
	if (handle.handle) CloseHandle(handle.handle);
}

bool SystemWriteFile(FileHandle handle, const void* data, u64 size, u64 offset) {
	DebugAssert(handle.handle != nullptr, "Invalid file handle.");
	DebugAssert(size <= (u64)u32_max, "File write size must be under 4GB. Size given '%llu'.", size);
	
	OVERLAPPED overlapped = {};
	overlapped.Offset     = (u32)offset;
	overlapped.OffsetHigh = (u32)(offset >> 32);
	
	DWORD size_written = 0;
	bool success = WriteFile((HANDLE)handle.handle, data, (DWORD)size, &size_written, &overlapped) != 0;
	
	return success && ((u64)size_written == size);
}

bool SystemReadFile(FileHandle handle, void* data, u64 size, u64 offset) {
	DebugAssert(handle.handle != nullptr, "Invalid file handle.");
	DebugAssert(size <= (u64)u32_max, "File read size must be under 4GB. Size given '%llu'.", size);
	
	OVERLAPPED overlapped = {};
	overlapped.Offset     = (u32)offset;
	overlapped.OffsetHigh = (u32)(offset >> 32);
	
	DWORD size_read = 0;
	bool success = ReadFile((HANDLE)handle.handle, data, (DWORD)size, &size_read, &overlapped) != 0;
	
	return success && ((u64)size_read == size);
}

u64 SystemFileSize(FileHandle handle) {
	DebugAssert(handle.handle != nullptr, "Invalid file handle.");
	
	LARGE_INTEGER size = {};
	bool success = GetFileSizeEx(handle.handle, &size) != 0;
	
	return success ? (u64)size.QuadPart : 0;
}

String SystemReadFileToString(StackAllocator* alloc, String path) {
	auto file = SystemOpenFile(alloc, path, OpenFileFlags::Read);
	if (file.handle == nullptr) return {};
	defer{ SystemCloseFile(file); };
	
	String result;
	result.count = SystemFileSize(file);
	result.data  = (char*)alloc->Allocate(result.count + 1);
	
	bool success = SystemReadFile(file, result.data, result.count, 0);
	DebugAssert(success, "Failed to read a file to string.");
	result.data[result.count] = '\0';
	
	return result;
}


struct DirectoryChangeTracker {
	HANDLE directory_handle   = nullptr;
	HANDLE io_completion_port = nullptr;
	
	OVERLAPPED overlapped = {};
	
	ArrayView<u8> buffer;
};

static bool ReadDirectoryChangesAsync(DirectoryChangeTracker* tracker) {
	auto filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;
	return ReadDirectoryChangesW(tracker->directory_handle, tracker->buffer.data, (DWORD)tracker->buffer.count, true, filter, nullptr, &tracker->overlapped, nullptr) != 0;
}

DirectoryChangeTracker* CreateDirectoryChangeTracker(StackAllocator* alloc, String directory_path) {
	HANDLE directory_handle = nullptr;
	{
		TempAllocationScope(alloc);
		auto directory_path_utf16 = StringUtf8ToUtf16(alloc, directory_path);
		directory_handle = CreateFileW((wchar_t*)directory_path_utf16.data, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
	}
	if (directory_handle == INVALID_HANDLE_VALUE) return nullptr;
	
	auto io_completion_port = CreateIoCompletionPort(directory_handle, nullptr, 0, 1);
	if (io_completion_port == nullptr) {
		CloseHandle(directory_handle);
		return nullptr;
	}
	
	auto* tracker = NewFromAlloc(alloc, DirectoryChangeTracker);
	tracker->directory_handle   = directory_handle;
	tracker->io_completion_port = io_completion_port;
	tracker->buffer.count = 4096;
	tracker->buffer.data  = (u8*)alloc->Allocate(tracker->buffer.count);
	
	ReadDirectoryChangesAsync(tracker);

	return tracker;
}

void ReleaseDirectoryChangeTracker(DirectoryChangeTracker* tracker) {
	CloseHandle(tracker->directory_handle);
	CloseHandle(tracker->io_completion_port);
}

static u32 CountFileNotifyRecords(u8* buffer) {
	u32 record_count = 0;
	
	bool has_next_record = true;
	while (has_next_record) {
		auto* record = (FILE_NOTIFY_INFORMATION*)buffer;
		has_next_record = record->NextEntryOffset != 0;
		buffer += record->NextEntryOffset;
		
		record_count += 1;
	}
	
	return record_count;
}

ArrayView<String> ReadDirectoryChangeEvents(StackAllocator* alloc, DirectoryChangeTracker* tracker) {
	DWORD number_of_bytes_transfered = 0;
	u64 completion_key = 0;
	OVERLAPPED* overlapped = nullptr;
	
	bool success = GetQueuedCompletionStatus(tracker->io_completion_port, &number_of_bytes_transfered, &completion_key, &overlapped, 0) != 0;
	if (success == false || number_of_bytes_transfered == 0) return {};
	
	u32 record_count = CountFileNotifyRecords(tracker->buffer.data);
	
	Array<String> changed_file_paths;
	ArrayResize(changed_file_paths, alloc, record_count);
	
	u8* buffer = tracker->buffer.data;
	for (auto& path : changed_file_paths) {
		auto* record = (FILE_NOTIFY_INFORMATION*)buffer;
		path = StringUtf16ToUtf8(alloc, StringUtf16{ (u16*)record->FileName, record->FileNameLength / sizeof(wchar_t) });
		buffer += record->NextEntryOffset;
	}
	
	ReadDirectoryChangesAsync(tracker);
	
	return changed_file_paths;
}


void SystemWriteToConsole(StackAllocator* alloc, String message) {
	TempAllocationScope(alloc);
	
	auto message_utf16 = StringUtf8ToUtf16(alloc, message);
	WriteConsoleW(GetStdHandle(STD_ERROR_HANDLE), message_utf16.data, (u32)message_utf16.count, nullptr, nullptr);
}

void SystemWriteToConsole(StackAllocator* alloc, const char* format, ...) {
	TempAllocationScope(alloc);
	
	va_list va_args;
	va_start(va_args, format);
	auto message = StringFormatV(alloc, format, va_args);
	va_end(va_args);
	
	SystemWriteToConsole(alloc, message);
}


void SystemExitProcess(u32 exit_code) {
	ExitProcess(exit_code);
}
