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
		buffer.cursor = (u8*)alloc->Allocate(file_size);
		buffer.remaining_size = (u32)file_size;
		
		success = SystemReadFile(file, buffer.cursor, file_size, 0);
	}
	
	return success;
}

bool CloseSaveLoadBuffer(SaveLoadBuffer& buffer) {
	ProfilerScope("CloseSaveLoadBuffer");
	
	if (buffer.is_saving) {
		auto file = SystemOpenFile(buffer.alloc, buffer.filepath, OpenFileFlags::Write);
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
	}
	
	return true;
}
