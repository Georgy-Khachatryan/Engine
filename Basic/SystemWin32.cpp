#include "Basic.h"
#include "BasicMemory.h"
#include "BasicFiles.h"
#include "BasicString.h"

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
	// - For read open an existing file. Allow sharing with other processes for reading.
	// - For write always create a new file. Don't allow shared reading or writing.
	//
	switch (flags & (OpenFileFlags::Read | OpenFileFlags::Write)) {
	case OpenFileFlags::Read:  access = GENERIC_READ;  sharing_mode = FILE_SHARE_READ; creation_mode = OPEN_EXISTING; break;
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

