#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApiTypes.h"
#include "TextureFormat.h"

struct AsyncTransferQueue;
struct GraphicsContext;

struct AsyncTransferSrcFile {
	FileHandle handle;
	u64 offset = 0;
	u64 size   = 0;
};

struct AsyncTransferSrcMemory {
	u8* address = nullptr;
	u64 size = 0;
};

enum struct AsyncTransferSrcType : u32 {
	File   = 0,
	Memory = 1,
};


struct AsyncTransferDstBuffer {
	NativeBufferResource resource;
	u64 size = 0; // Size of the whole buffer resource.
	
	u64 offset = 0;
};

enum struct AsyncTransferDstType : u32 {
	Buffer = 0,
};


struct AsyncTransferCommand {
	AsyncTransferSrcType src_type = AsyncTransferSrcType::File;
	AsyncTransferDstType dst_type = AsyncTransferDstType::Buffer;
	
	union {
		AsyncTransferSrcFile file = {};
		AsyncTransferSrcMemory memory;
	} src;
	
	union {
		AsyncTransferDstBuffer buffer = {};
	} dst;
};

u64 AppendAsyncTransferCommand(AsyncTransferQueue* queue, const AsyncTransferCommand& command);
u64 AsyncCopyMemoryToBuffer(AsyncTransferQueue* async_transfer_queue, NativeBufferResource dst_buffer, u64 dst_buffer_offset, u64 dst_buffer_size, void* src_data, u64 copy_size);
u64 AsyncCopyFileToBuffer(AsyncTransferQueue* async_transfer_queue, NativeBufferResource dst_buffer, u64 dst_buffer_offset, u64 dst_buffer_size, FileHandle src_file, u64 src_file_offset, u64 copy_size);

u64 CompletedGpuAsyncTransferIndex(AsyncTransferQueue* async_transfer_queue);

AsyncTransferQueue* CreateAsyncTransferQueue(StackAllocator* alloc, GraphicsContext* graphics_context);
void ReleaseAsyncTransferQueue(AsyncTransferQueue* queue);
void UpdateAsyncTransferQueue(AsyncTransferQueue* queue);

