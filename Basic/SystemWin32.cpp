#include "Basic.h"
#include "BasicArray.h"
#include "BasicMemory.h"
#include "BasicFiles.h"
#include "BasicString.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ioringapi.h>


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
	default: DebugAssertAlways("Invalid combination of open file flags '%'.", (u32)flags);
	}
	
	if (HasAnyFlags(flags, OpenFileFlags::Async)) {
		attributes |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED;
	}
	
	auto path_utf16 = StringUtf8ToUtf16(alloc, path);
	
	auto handle = CreateFileW((wchar_t*)path_utf16.data, access, sharing_mode, nullptr, creation_mode, attributes, nullptr);
	
	return FileHandle{ handle == INVALID_HANDLE_VALUE ? nullptr : handle };
}

bool SystemCloseFile(FileHandle handle) {
	return handle.handle ? CloseHandle(handle.handle) != 0 : true;
}

bool SystemWriteFile(FileHandle handle, const void* data, u64 size, u64 offset) {
	DebugAssert(handle.handle != nullptr, "Invalid file handle.");
	DebugAssert(size <= (u64)u32_max, "File write size must be under 4GB. Size given '%'.", size);
	
	OVERLAPPED overlapped = {};
	overlapped.Offset     = (u32)offset;
	overlapped.OffsetHigh = (u32)(offset >> 32);
	
	DWORD size_written = 0;
	bool success = WriteFile((HANDLE)handle.handle, data, (DWORD)size, &size_written, &overlapped) != 0;
	
	return success && ((u64)size_written == size);
}

bool SystemReadFile(FileHandle handle, void* data, u64 size, u64 offset) {
	DebugAssert(handle.handle != nullptr, "Invalid file handle.");
	DebugAssert(size <= (u64)u32_max, "File read size must be under 4GB. Size given '%'.", size);
	
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
	
	auto result = StringAllocate(alloc, SystemFileSize(file));
	
	bool success = SystemReadFile(file, result.data, result.count, 0);
	DebugAssert(success, "Failed to read a file to string.");
	
	return result;
}

bool SystemCreateDirectory(StackAllocator* alloc, String path) {
	TempAllocationScope(alloc);
	
	auto path_utf16 = StringUtf8ToUtf16(alloc, path);
	
	bool success = CreateDirectoryW((wchar_t*)path_utf16.data, nullptr) != 0;
	if (success == false) success |= (GetLastError() == ERROR_ALREADY_EXISTS);
	
	return success;
}


struct FileIoQueue {
	ArrayView<IoOperationStatus> io_status_array;
	Array<u16> io_status_free_indices;
	
	HIORING handle = nullptr;
	void* buffer = nullptr;
	u64 buffer_size = 0;
};

FileIoQueue* SystemCreateFileIoQueue(StackAllocator* alloc, u32 queue_size, u8* buffer, u64 buffer_size) {
	ProfilerScope("SystemCreateFileIoQueue");
	
	IORING_CREATE_FLAGS flags = {};
	flags.Required = IORING_CREATE_REQUIRED_FLAGS_NONE;
#if BUILD_TYPE(DEBUG) || BUILD_TYPE(DEV)
	flags.Advisory = IORING_CREATE_ADVISORY_FLAGS_NONE;
#elif BUILD_TYPE(PROFILE)
	flags.Advisory = IORING_CREATE_SKIP_BUILDER_PARAM_CHECKS;
#else // !BUILD_TYPE(PROFILE)
#error "Unknown BUILD_TYPE."
#endif // !BUILD_TYPE(PROFILE)
	
	HIORING handle = nullptr;
	auto result = CreateIoRing(IORING_VERSION_2, flags, queue_size, queue_size * 2, &handle);
	if (FAILED(result)) return nullptr;
	
	auto* queue = NewFromAlloc(alloc, FileIoQueue);
	queue->handle = handle;
	
	queue->io_status_array = ArrayViewAllocate<IoOperationStatus>(alloc, queue_size * 2);
	memset(queue->io_status_array.data, (u8)IoOperationStatus::Free, queue->io_status_array.count);
	
	ArrayResize(queue->io_status_free_indices, alloc, queue_size * 2);
	for (u32 i = 0; i < queue->io_status_free_indices.capacity; i += 1) {
		queue->io_status_free_indices[i] = (u16)i;
	}
	
	memset(buffer, 0, buffer_size);
	
	IORING_BUFFER_INFO buffer_info;
	buffer_info.Address = buffer;
	buffer_info.Length  = (u32)buffer_size;
	BuildIoRingRegisterBuffers(handle, 1, &buffer_info, 0);
	
	SubmitIoRing(handle, 1, INFINITE, nullptr);
	
	IORING_CQE entry = {};
	PopIoRingCompletion(queue->handle, &entry);
	
	queue->buffer      = buffer;
	queue->buffer_size = buffer_size;
	
	return queue;
}

void SystemReleaseFileIoQueue(FileIoQueue* queue) {
	auto result = CloseIoRing(queue->handle);
	DebugAssert(SUCCEEDED(result), "Failed to close IO Ring.");
}

u64 CmdSystemReadFile(FileIoQueue* queue, FileHandle file, u64 buffer_offset, u64 size, u64 offset) {
	ProfilerScope("CmdSystemReadFile");
	
	DebugAssert(size <= (u64)u32_max, "File read size must be under 4GB. Size given '%'.", size);
	
	if (queue->io_status_free_indices.count == 0) return u64_max;
	
	auto file_ref   = IoRingHandleRefFromHandle((HANDLE)file.handle);
	auto buffer_ref = IoRingBufferRefFromIndexAndOffset(0u, (u32)buffer_offset);
	
	u64 io_completion_index = ArrayLastElement(queue->io_status_free_indices);
	auto result = BuildIoRingReadFile(queue->handle, file_ref, buffer_ref, (u32)size, offset, io_completion_index, IOSQE_FLAGS_NONE);
	if (FAILED(result)) return u64_max;
	
	ArrayPopLast(queue->io_status_free_indices);
	queue->io_status_array[io_completion_index] = IoOperationStatus::InFlight;
	
	return io_completion_index;
}

u32 SystemSubmitFileIoQueue(FileIoQueue* queue) {
	ProfilerScope("SystemSubmitFileIoQueue");
	
	u32 submitted_count = 0;
	auto result = SubmitIoRing(queue->handle, 0, 0, &submitted_count);
	
	return SUCCEEDED(result) ? submitted_count : 0;
}

void SystemCheckIoCompletion(FileIoQueue* queue) {
	ProfilerScope("SystemCheckIoCompletion");
	
	IORING_CQE entry = {};
	while (PopIoRingCompletion(queue->handle, &entry) == S_OK) {
		queue->io_status_array[entry.UserData] = SUCCEEDED(entry.ResultCode) ? IoOperationStatus::Succeeded : IoOperationStatus::Failed;
	}
}

IoOperationStatus SystemConfirmIoCompletion(FileIoQueue* queue, u64 io_completion_index) {
	auto status = queue->io_status_array[io_completion_index];
	
	if (status == IoOperationStatus::Failed || status == IoOperationStatus::Succeeded) {
		queue->io_status_array[io_completion_index] = IoOperationStatus::Free;
		ArrayAppend(queue->io_status_free_indices, (u16)io_completion_index);
	}
	
	return status;
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
	tracker->buffer             = ArrayViewAllocate<u8>(alloc, 4096u);
	
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


void SystemWriteToConsole(String message) {
	SetConsoleOutputCP(CP_UTF8); // TODO: Set char page on startup.
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data, (u32)message.count, nullptr, nullptr);
}

void SystemWriteToConsoleV(StackAllocator* alloc, String format, ArrayView<StringFormatArgument> arguments) {
	TempAllocationScope(alloc);
	SystemWriteToConsole(StringFormatV(alloc, format, arguments));
}


void SystemExitProcess(u32 exit_code) {
	ExitProcess(exit_code);
}
