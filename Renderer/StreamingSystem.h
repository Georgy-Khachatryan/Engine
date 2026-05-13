#pragma once
#include "Basic/Basic.h"

inline bool IsWaitComplete(u64 wait_index, u64 current_frame_index, u64 completed_file_read_index) {
	return (wait_index >> 1) <= (wait_index & 0x1 ? completed_file_read_index : current_frame_index);
}

inline u64 EncodeGpuFrameWaitIndex(u64 current_frame_index) {
	return (current_frame_index + number_of_frames_in_flight) << 1;
}

inline u64 EncodeFileReadWaitIndex(u64 file_read_index) {
	return (file_read_index << 1) | 0x1;
}
