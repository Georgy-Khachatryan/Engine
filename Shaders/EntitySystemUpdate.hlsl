#include "Basic.hlsl"

[ThreadGroupSize(128, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id >= constants.count) return;
	
	uint dst_index  = dst_indices.Load(thread_id * sizeof(uint));
	uint src_offset = thread_id * constants.stride;
	uint dst_offset = dst_index * constants.stride;
	
	uint remaining_size = constants.stride;
	while (remaining_size >= sizeof(uint4)) {
		dst_data.Store4(dst_offset, src_data.Load4(src_offset));
		src_offset += sizeof(uint4);
		dst_offset += sizeof(uint4);
		remaining_size -= sizeof(uint4);
	}
	
	while (remaining_size >= sizeof(uint)) {
		dst_data.Store(dst_offset, src_data.Load(src_offset));
		src_offset += sizeof(uint);
		dst_offset += sizeof(uint);
		remaining_size -= sizeof(uint);
	}
}
