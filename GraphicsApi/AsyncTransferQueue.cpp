#include "AsyncTransferQueue.h"
#include "GraphicsApi.h"

template<typename T>
struct RingBuffer {
	T*  data         = nullptr;
	u64 read_offset  = 0;
	u64 write_offset = 0;
	u64 capacity     = 0;
	
	T& operator[] (u64 index) { DebugAssert(index < write_offset, "RingBuffer access out of bounds. (%/%).", index, write_offset); return data[index & (capacity - 1)]; }
};

template<typename T, typename AllocatorT>
void RingBufferReserve(RingBuffer<T>& buffer, AllocatorT* alloc, u64 new_capacity) {
	DebugAssert((new_capacity & (new_capacity - 1)) == 0, "Trying to reserve non power of two ring buffer. %..", new_capacity);
	if (buffer.capacity >= new_capacity) return;
	buffer.data = (T*)alloc->Reallocate(buffer.data, buffer.capacity * sizeof(T), new_capacity * sizeof(T), alignof(T));
	buffer.capacity = new_capacity;
}

template<typename T>
u64 RingBufferAppend(RingBuffer<T>& buffer, const T& element) {
	DebugAssert((buffer.write_offset - buffer.read_offset) < buffer.capacity, "RingBufferAppend overflowed allocated buffer: %..", buffer.capacity);
	
	u64 write_offset = buffer.write_offset++;
	buffer[write_offset] = element;
	
	return write_offset;
}

template<typename T>
static u64 RingBufferTryAppendMultiple(RingBuffer<T>& buffer, u64 count) {
	if ((buffer.write_offset - buffer.read_offset) + count > buffer.capacity) return u64_max;
	
	u64 write_offset = buffer.write_offset;
	buffer.write_offset = write_offset + count;
	
	return write_offset;
}


compile_const u64 max_async_transfer_command_count  = 512;
compile_const u64 async_upload_buffer_capacity      = 32 * 1024 * 1024;
compile_const u64 max_upload_buffer_allocation_size = async_upload_buffer_capacity / 4;

struct AsyncTransferExecutionState : AsyncTransferCommand {
	u64 upload_buffer_offset = 0;
	u64 size_with_padding    = 0;
	
	u64 file_io_submit_wait_index = 0;
	u64 gpu_submit_wait_index     = u64_max;
};

struct UploadRingBuffer {
	NativeBufferResource resource;
	
	u8* cpu_address  = nullptr;
	u64 read_offset  = 0;
	u64 write_offset = 0;
};

struct AsyncTransferQueue {
	RingBuffer<AsyncTransferCommand>   user_command_ring;
	RingBuffer<AsyncTransferExecutionState> command_ring;
	u64 allocation_write_offset = 0;
	u64 file_read_write_offset  = 0;
	u64 file_wait_write_offset  = 0;
	
	Array<AsyncCopyBufferToBufferCommand> copy_buffer_to_buffer_commands;
	
	UploadRingBuffer upload_ring_buffer;
	
	FileIoQueue*     file_io_queue    = nullptr;
	GraphicsContext* graphics_context = nullptr;
};

AsyncTransferQueue* CreateAsyncTransferQueue(StackAllocator* alloc, GraphicsContext* graphics_context) {
	ProfilerScope("CreateAsyncTransferQueue");
	
	auto* queue = NewFromAlloc(alloc, AsyncTransferQueue);
	RingBufferReserve(queue->user_command_ring, alloc, max_async_transfer_command_count);
	RingBufferReserve(queue->command_ring, alloc, max_async_transfer_command_count * 2);
	
	// Can't signal 0 completion value on a u64 fence, start from 1.
	queue->user_command_ring.write_offset = 1;
	queue->user_command_ring.read_offset  = 1;
	
	ArrayReserve(queue->copy_buffer_to_buffer_commands, alloc, max_async_transfer_command_count);
	
	u8* upload_buffer_cpu_address = nullptr;
	queue->upload_ring_buffer.resource = CreateBufferResource(graphics_context, async_upload_buffer_capacity, GpuMemoryAccessType::Upload, &upload_buffer_cpu_address);
	queue->upload_ring_buffer.cpu_address = upload_buffer_cpu_address;
	
	queue->file_io_queue = SystemCreateFileIoQueue(alloc, 64, upload_buffer_cpu_address, async_upload_buffer_capacity);
	queue->graphics_context = graphics_context;
	
	return queue;
}

void ReleaseAsyncTransferQueue(AsyncTransferQueue* queue) {
	SystemReleaseFileIoQueue(queue->file_io_queue);
	ReleaseBufferResource(queue->graphics_context, queue->upload_ring_buffer.resource);
}

static u64 ComputeTransferCommandSize(const AsyncTransferCommand& command) {
	u64 command_size = 0;
	switch (command.src_type) {
	case AsyncTransferSrcType::File:   command_size = command.src.file.size;   break;
	case AsyncTransferSrcType::Memory: command_size = command.src.memory.size; break;
	}
	return command_size;
}

static void SplitAsyncTransferCommands(AsyncTransferQueue* queue) {
	ProfilerScope("SplitAsyncTransferCommands");
	
	auto& user_command_ring = queue->user_command_ring;
	auto& command_ring      = queue->command_ring;
	
	u64 write_offset = user_command_ring.write_offset;
	u64 read_offset  = user_command_ring.read_offset;
	
	for (u64 i = read_offset; i < write_offset; i += 1) {
		auto& command = user_command_ring[i];
		
		u64 command_size = ComputeTransferCommandSize(command);
		
		u64 allocation_command_count = DivideAndRoundUp(command_size, max_upload_buffer_allocation_size);
		u64 write_offset = RingBufferTryAppendMultiple(command_ring, allocation_command_count);
		if (write_offset == u64_max) break; // Stop on ring buffer overflow.
		
		
		for (u64 allocation_index = 0; allocation_index < allocation_command_count; allocation_index += 1) {
			auto allocation_command = AsyncTransferExecutionState{ command };
			
			u64 sub_command_offset = allocation_index * max_upload_buffer_allocation_size;
			u64 sub_command_size   = Min(command_size - sub_command_offset, max_upload_buffer_allocation_size);
			
			// Offset src data:
			switch (allocation_command.src_type) {
			case AsyncTransferSrcType::File: {
				allocation_command.src.file.offset += sub_command_offset;
				allocation_command.src.file.size = sub_command_size;
				break;
			} case AsyncTransferSrcType::Memory: {
				allocation_command.src.memory.address += sub_command_offset;
				allocation_command.src.memory.size = sub_command_size;
				break;
			}
			}
			
			// Offset dst data:
			switch (allocation_command.dst_type) {
			case AsyncTransferDstType::Buffer: {
				allocation_command.dst.buffer.offset += sub_command_offset;
				break;
			}
			}
			
			if (allocation_index + 1 >= allocation_command_count) {
				allocation_command.gpu_submit_wait_index = i;
			}
			
			command_ring[write_offset + allocation_index] = allocation_command;
		}
		
		read_offset += 1;
	}
	user_command_ring.read_offset = read_offset;
}

void UpdateAsyncTransferQueue(AsyncTransferQueue* queue) {
	ProfilerScope("UpdateAsyncTransferQueue");
	
	SplitAsyncTransferCommands(queue);
	
	auto& command_ring       = queue->command_ring;
	auto& upload_ring_buffer = queue->upload_ring_buffer;
	auto* file_io_queue      = queue->file_io_queue;
	
	// Ranges of commands in different states:
	u64 write_offset            = command_ring.write_offset;
	u64 allocation_write_offset = queue->allocation_write_offset;
	u64 file_read_write_offset  = queue->file_read_write_offset;
	u64 file_wait_write_offset  = queue->file_wait_write_offset;
	u64 read_offset             = command_ring.read_offset;
	
	// Allocate upload heap space from the ring buffer for each command:
	{
		for (u64 i = allocation_write_offset; i < write_offset; i += 1) {
			auto& command = command_ring[i];
			
			u64 command_size = ComputeTransferCommandSize(command);
			DebugAssert(command_size <= max_upload_buffer_allocation_size, "Source size is too large. (%/%).", command_size, max_upload_buffer_allocation_size);
			
			u64 aligned_command_size = AlignUp(command_size, 4096);
			
			u64 offset_0 = upload_ring_buffer.write_offset;
			u64 offset_1 = offset_0 + aligned_command_size;
			
			u64 epoch_index_0 = (offset_0 / async_upload_buffer_capacity);
			u64 epoch_index_1 = (offset_1 - 1) / async_upload_buffer_capacity;
			
			if (epoch_index_0 != epoch_index_1) { // Retry on wraparound.
				offset_0 = epoch_index_1 * async_upload_buffer_capacity;
				offset_1 = offset_0 + aligned_command_size;
			}
			
			if (offset_1 - upload_ring_buffer.read_offset > async_upload_buffer_capacity) break; // Out of space in the upload buffer.
			
			command.upload_buffer_offset = offset_0 % async_upload_buffer_capacity;
			command.size_with_padding    = offset_1 - upload_ring_buffer.write_offset;
			
			upload_ring_buffer.write_offset = offset_1;
			
			allocation_write_offset += 1;
		}
		queue->allocation_write_offset = allocation_write_offset;
	}
	
	// Issue file read commands:
	{
		bool has_any_pending_file_reads = false;
		for (u64 i = file_read_write_offset; i < allocation_write_offset; i += 1) {
			auto& command = command_ring[i];
			
			if (command.src_type == AsyncTransferSrcType::File) {
				u64 wait_index = CmdSystemReadFile(file_io_queue, command.src.file.handle, command.upload_buffer_offset, command.src.file.size, command.src.file.offset);
				if (wait_index == u64_max) break;
				
				command.file_io_submit_wait_index = wait_index;
				has_any_pending_file_reads = true;
			} else if (command.src_type == AsyncTransferSrcType::Memory) {
				u8* dst_cpu_address = upload_ring_buffer.cpu_address + command.upload_buffer_offset;
				memcpy(dst_cpu_address, command.src.memory.address, command.src.memory.size);
			}
			
			file_read_write_offset += 1;
		}
		queue->file_read_write_offset = file_read_write_offset;
		
		if (has_any_pending_file_reads) {
			SystemSubmitFileIoQueue(file_io_queue);
		}
	}
	
	SystemCheckIoCompletion(file_io_queue);
	
	// Issue GPU copy commands:
	{
		auto copy_buffer_to_buffer_commands = queue->copy_buffer_to_buffer_commands;
		
		u64 last_gpu_submit_wait_index = 0;
		for (u64 i = file_wait_write_offset; i < file_read_write_offset; i += 1) {
			auto& command = command_ring[i];
			
			if (command.src_type == AsyncTransferSrcType::File) {
				auto io_status = SystemConfirmIoCompletion(file_io_queue, command.file_io_submit_wait_index);
				if (io_status == IoOperationStatus::InFlight) break;
				
				DebugAssert(io_status == IoOperationStatus::Succeeded, "Async file read failed.");
			}
			
			AsyncCopyBufferToBufferCommand copy_command;
			copy_command.src_resource = upload_ring_buffer.resource;
			copy_command.dst_resource = command.dst.buffer.resource;
			copy_command.src_offset   = command.upload_buffer_offset;
			copy_command.dst_offset   = command.dst.buffer.offset;
			copy_command.size         = ComputeTransferCommandSize(command);
			ArrayAppend(copy_buffer_to_buffer_commands, copy_command);
			
			if (command.gpu_submit_wait_index != u64_max) {
				last_gpu_submit_wait_index = command.gpu_submit_wait_index;
			}
			
			file_wait_write_offset += 1;
		}
		queue->file_wait_write_offset = file_wait_write_offset;
		
		if (copy_buffer_to_buffer_commands.count) {
			SubmitAsyncCopyCommands(queue->graphics_context, copy_buffer_to_buffer_commands, last_gpu_submit_wait_index);
		}
	}
	
	// Release command ring elements and upload ring buffer space:
	{
		u64 gpu_submit_wait_index = GetCompletedAsyncCopyCommandValue(queue->graphics_context);
		for (u64 i = read_offset; i < file_wait_write_offset; i += 1) {
			auto& command = command_ring[i];
			
			if (command.gpu_submit_wait_index >= gpu_submit_wait_index) break;
			upload_ring_buffer.read_offset += command.size_with_padding;
			
			read_offset += 1;
		}
		command_ring.read_offset = read_offset;
	}
}

u64 AppendAsyncTransferCommand(AsyncTransferQueue* queue, const AsyncTransferCommand& command) {
	return RingBufferAppend(queue->user_command_ring, command);
}
