#include "Basic.h"
#include "BasicSaveLoad.h"
#include "BasicFiles.h"


bool OpenSaveLoadBuffer(StackAllocator* alloc, String filepath, SaveLoadDirection direction, SaveLoadBuffer& buffer) {
	ProfilerScope("OpenSaveLoadBuffer");
	
	buffer = {};
	buffer.alloc     = alloc;
	buffer.direction = direction;
	buffer.filepath  = filepath;
	
	bool success = true;
	if (direction == SaveLoadDirection::Loading) {
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
	if (buffer.direction == SaveLoadDirection::Saving) {
		auto file = SystemOpenFile(buffer.alloc, buffer.filepath, OpenFileFlags::Write);
		if (file.handle == nullptr) return false;
		defer{ SystemCloseFile(file); };
		
		success = SystemWriteFile(file, buffer.data.data, buffer.data.count, 0);
	}
	
	return true;
}
