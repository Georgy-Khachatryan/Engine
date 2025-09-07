#include "Basic.h"
#include "BasicMemory.h"

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
