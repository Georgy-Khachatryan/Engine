#include "Basic.hlsl"

groupshared float gs_samples[1024 / 16];
groupshared uint gs_thread_group_exit_index;

[ThreadGroupSize(1024, 1, 1)][WaveSize(16, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * 32 + MortonDecode(thread_index);
	
	// TODO: Load from a constant buffer.
	uint2 culling_hzb_size; culling_hzb[0].GetDimensions(culling_hzb_size.x, culling_hzb_size.y);
	
	float4 sample_rect = float4(thread_id + 0.25, thread_id + 0.75) * (1.0 / culling_hzb_size).xyxy;
	
	float s0 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.xy, 0);
	float s1 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.xw, 0);
	float s2 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.zy, 0);
	float s3 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.zw, 0);
	float s = min(min(s0, s1), min(s2, s3));
	
	culling_hzb[0][thread_id >> 0] = s;
	
	s = min(s, WaveShuffleXor(s, 0x1));
	s = min(s, WaveShuffleXor(s, 0x2));
	
	if ((thread_index & 0x3) == 0) {
		culling_hzb[1][thread_id >> 1] = s;
	}
	
	s = min(s, WaveShuffleXor(s, 0x4));
	s = min(s, WaveShuffleXor(s, 0x8));
	
	if ((thread_index & 0xF) == 0) {
		culling_hzb[2][thread_id >> 2] = s;
		gs_samples[thread_index >> 4] = s;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < 16) {
		thread_id = group_id * 4 + MortonDecode(thread_index);
		
		s0 = gs_samples[thread_index * 4 + 0];
		s1 = gs_samples[thread_index * 4 + 1];
		s2 = gs_samples[thread_index * 4 + 2];
		s3 = gs_samples[thread_index * 4 + 3];
		s = min(min(s0, s1), min(s2, s3));
		
		culling_hzb[3][thread_id >> 0] = s;
		
		s = min(s, WaveShuffleXor(s, 0x1));
		s = min(s, WaveShuffleXor(s, 0x2));
		
		if ((thread_index & 0x3) == 0) {
			culling_hzb[4][thread_id >> 1] = s;
		}
		
		s = min(s, WaveShuffleXor(s, 0x4));
		s = min(s, WaveShuffleXor(s, 0x8));
		
		if ((thread_index & 0xF) == 0) {
			culling_hzb[5][thread_id >> 2] = s;
			
			InterlockedAdd(culling_hzb_build_state[0], 1u, gs_thread_group_exit_index);
			
			uint original = 0;
			InterlockedExchange(culling_hzb_build_state[MortonEncode(group_id) + 1], asuint(s), original);
		}
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (gs_thread_group_exit_index != constants.last_thread_group_index) return;
	thread_id = MortonDecode(thread_index);
	
	uint s0_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 0) + 1], 0u, s0_uint);
	uint s1_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 1) + 1], 0u, s1_uint);
	uint s2_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 2) + 1], 0u, s2_uint);
	uint s3_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 3) + 1], 0u, s3_uint);
	
	s0 = asfloat(s0_uint);
	s1 = asfloat(s1_uint);
	s2 = asfloat(s2_uint);
	s3 = asfloat(s3_uint);
	s = min(min(s0, s1), min(s2, s3));
	
	culling_hzb[6][thread_id >> 0] = s;
	
	s = min(s, WaveShuffleXor(s, 0x1));
	s = min(s, WaveShuffleXor(s, 0x2));
	
	if ((thread_index & 0x3) == 0) {
		culling_hzb[7][thread_id >> 1] = s;
	}
	
	s = min(s, WaveShuffleXor(s, 0x1));
	s = min(s, WaveShuffleXor(s, 0x2));
	
	s = min(s, WaveShuffleXor(s, 0x4));
	s = min(s, WaveShuffleXor(s, 0x8));
	
	if ((thread_index & 0xF) == 0) {
		culling_hzb[8][thread_id >> 2] = s;
		gs_samples[thread_index >> 4] = s;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < 16) {
		s0 = gs_samples[thread_index * 4 + 0];
		s1 = gs_samples[thread_index * 4 + 1];
		s2 = gs_samples[thread_index * 4 + 2];
		s3 = gs_samples[thread_index * 4 + 3];
		s = min(min(s0, s1), min(s2, s3));
		
		culling_hzb[9][thread_id >> 0] = s;
		
		s = min(s, WaveShuffleXor(s, 0x1));
		s = min(s, WaveShuffleXor(s, 0x2));
		
		if ((thread_index & 0x3) == 0) {
			culling_hzb[10][thread_id >> 1] = s;
		}
		
		s = min(s, WaveShuffleXor(s, 0x4));
		s = min(s, WaveShuffleXor(s, 0x8));
		
		if ((thread_index & 0xF) == 0) {
			culling_hzb[11][thread_id >> 2] = s;
		}
	}
}
