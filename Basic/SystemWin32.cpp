#include "Basic.h"
#include "BasicMemory.h"
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
