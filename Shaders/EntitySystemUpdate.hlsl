#include "Basic.hlsl"
#include "Generated/EntitySystemUpdateData.hlsl"

template<typename SrcByteBufferT>
void Memcpy(RWByteAddressBuffer dst_buffer, uint dst_offset, SrcByteBufferT src_buffer, uint src_offset, uint remaining_size) {
	while (remaining_size >= sizeof(uint4)) {
		dst_buffer.Store4(dst_offset, src_buffer.Load4(src_offset));
		src_offset += sizeof(uint4);
		dst_offset += sizeof(uint4);
		remaining_size -= sizeof(uint4);
	}
	
	while (remaining_size >= sizeof(uint)) {
		dst_buffer.Store(dst_offset, src_buffer.Load(src_offset));
		src_offset += sizeof(uint);
		dst_offset += sizeof(uint);
		remaining_size -= sizeof(uint);
	}
}

[ThreadGroupSize(128, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id >= constants.count) return;
	
	uint dst_index_and_flags = dst_indices.Load(thread_id * sizeof(uint));
	uint dst_index = dst_index_and_flags & 0x3FFFFFFFu;
	uint dst_flags = dst_index_and_flags >> 30u;
	
	uint src_offset = thread_id * constants.stride;
	uint dst_offset = dst_index * constants.stride;
	
	if (dst_flags & GpuComponentUpdateFlags::InitHistory) {
		Memcpy(dst_prev_data, dst_offset, src_data, src_offset, constants.stride);
	} else if (dst_flags & GpuComponentUpdateFlags::CopyHistory) {
		Memcpy(dst_prev_data, dst_offset, dst_data, dst_offset, constants.stride);
	}
	
	Memcpy(dst_data, dst_offset, src_data, src_offset, constants.stride);
}
