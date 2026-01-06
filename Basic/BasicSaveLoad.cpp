#include "Basic.h"
#include "BasicSaveLoad.h"
#include "BasicFiles.h"


bool OpenSaveLoadBuffer(StackAllocator* alloc, String filepath, bool is_loading, SaveLoadBuffer& buffer) {
	ProfilerScope("OpenSaveLoadBuffer");
	
	buffer = {};
	buffer.alloc      = alloc;
	buffer.is_saving  = is_loading == false;
	buffer.is_loading = is_loading;
	buffer.filepath   = filepath;
	
	bool success = true;
	if (is_loading) {
		auto file = SystemOpenFile(alloc, filepath, OpenFileFlags::Read);
		if (file.handle == nullptr) return false;
		defer{ SystemCloseFile(file); };
		
		u64 file_size = SystemFileSize(file);
		buffer.data.data     = (u8*)alloc->Allocate(file_size);
		buffer.data.count    = 0;
		buffer.data.capacity = file_size;
		
		success = SystemReadFile(file, buffer.data.data, file_size, 0);
	}
	
	return success;
}

bool CloseSaveLoadBuffer(SaveLoadBuffer& buffer) {
	ProfilerScope("CloseSaveLoadBuffer");
	
	bool success = true;
	if (buffer.is_saving) {
		auto file = SystemOpenFile(buffer.alloc, buffer.filepath, OpenFileFlags::Write);
		if (file.handle == nullptr) return false;
		defer{ SystemCloseFile(file); };
		
		success = SystemWriteFile(file, buffer.data.data, buffer.data.count, 0);
	}
	
	return true;
}

void ResetSaveLoadBuffer(SaveLoadBuffer& buffer, u64 new_count) {
	DebugAssert(new_count <= buffer.data.count, "Resetting SaveLoad buffer to a higher count than before. (%/%).", new_count, buffer.data.count);
	
	buffer.data.count = new_count;
	buffer.is_saving  = !buffer.is_saving;
	buffer.is_loading = !buffer.is_loading;
}
