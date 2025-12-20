#include "Basic.h"
#include "BasicSaveLoad.h"
#include "BasicFiles.h"


SaveLoadBuffer OpenSaveLoadBufferForSaving(StackAllocator* alloc) {
	SaveLoadBuffer buffer;
	buffer.alloc      = alloc;
	buffer.is_saving  = true;
	buffer.is_loading = false;
	
	return buffer;
}

bool WriteSaveLoadBufferToFile(StackAllocator* alloc, SaveLoadBuffer& buffer, String path) {
	auto file = SystemOpenFile(alloc, path, OpenFileFlags::Write);
	if (file.handle == nullptr) return false;
	defer{ SystemCloseFile(file); };
	
	if (buffer.remaining_size != 0) {
		auto& last_chunk = ArrayLastElement(buffer.chunks);
		last_chunk.count -= buffer.remaining_size;
	}
	
	u64 offset = 0;
	for (auto [data, count] : buffer.chunks) {
		bool success = SystemWriteFile(file, data, count, offset);
		if (success == false) return false;
		
		offset += count;
	}
	
	return true;
}

bool OpenSaveLoadBufferForLoading(StackAllocator* alloc, String path, SaveLoadBuffer& buffer) {
	buffer = {};
	buffer.alloc      = alloc;
	buffer.is_saving  = false;
	buffer.is_loading = true;
	
	auto file = SystemOpenFile(alloc, path, OpenFileFlags::Read);
	if (file.handle == nullptr) return false;
	defer{ SystemCloseFile(file); };
	
	u64 file_size = SystemFileSize(file);
	buffer.cursor = (u8*)alloc->Allocate(file_size);
	buffer.remaining_size = file_size;
	
	return SystemReadFile(file, buffer.cursor, file_size, 0);
}

