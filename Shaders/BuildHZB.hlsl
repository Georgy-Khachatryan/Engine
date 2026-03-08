#include "Basic.hlsl"

groupshared float gs_samples[64];
groupshared uint gs_thread_group_exit_index;

// Down sample 64 input samples in 16 lanes to a single value and write 3 MIPs.
float WaveDownSample64x3(uint2 thread_id, uint thread_index, uint result_offset, float sample0, float sample1, float sample2, float sample3) {
	float sample = min(min(sample0, sample1), min(sample2, sample3));
	
	culling_hzb[result_offset + 0][thread_id >> 0] = sample;
	
	sample = min(sample, WaveShuffleXor(sample, 0x1));
	sample = min(sample, WaveShuffleXor(sample, 0x2));
	
	if ((thread_index & 0x3) == 0) {
		culling_hzb[result_offset + 1][thread_id >> 1] = sample;
	}
	
	sample = min(sample, WaveShuffleXor(sample, 0x4));
	sample = min(sample, WaveShuffleXor(sample, 0x8));
	
	if ((thread_index & 0xF) == 0) {
		culling_hzb[result_offset + 2][thread_id >> 2] = sample;
	}
	
	return sample;
}

// Down sample 4096 input samples in 1024 lanes to a single value and write 6 MIPs.
float GroupDownSample4096x6(uint2 thread_id, uint2 group_id, uint thread_index, uint result_offset, float sample0, float sample1, float sample2, float sample3) {
	float sample = WaveDownSample64x3(thread_id, thread_index, result_offset, sample0, sample1, sample2, sample3);
	
	if ((thread_index & 0xF) == 0) {
		gs_samples[thread_index >> 4] = sample;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < 16) {
		thread_id = group_id * 4 + MortonDecode(thread_index);
		
		sample0 = gs_samples[thread_index * 4 + 0];
		sample1 = gs_samples[thread_index * 4 + 1];
		sample2 = gs_samples[thread_index * 4 + 2];
		sample3 = gs_samples[thread_index * 4 + 3];
		sample = WaveDownSample64x3(thread_id, thread_index, result_offset + 3, sample0, sample1, sample2, sample3);
	}
	
	return sample;
}

[ThreadGroupSize(1024, 1, 1)][WaveSize(16, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * 32 + MortonDecode(thread_index);
	
	float2 hzb_to_screen_pixels = scene.inv_culling_hzb_size * scene.render_target_size;
	float2 sample_min  = round((thread_id + 0) * hzb_to_screen_pixels);
	float2 sample_max  = round((thread_id + 1) * hzb_to_screen_pixels);
	float4 sample_rect = float4(sample_min + 1.0, sample_max - 1.0) * scene.inv_render_target_size.xyxy;
	
	float sample0 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.xy, 0);
	float sample1 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.xw, 0);
	float sample2 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.zy, 0);
	float sample3 = depth_stencil.SampleLevel(sampler_min_clamp, sample_rect.zw, 0);
	float sample = GroupDownSample4096x6(thread_id, group_id, thread_index, 0, sample0, sample1, sample2, sample3);
	
	if (thread_index == 0) {
		InterlockedAdd(culling_hzb_build_state[0], 1u, gs_thread_group_exit_index);
		
		uint original = 0;
		InterlockedExchange(culling_hzb_build_state[MortonEncode(group_id) + 1], asuint(sample), original);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (gs_thread_group_exit_index != constants.last_thread_group_index) return;
	thread_id = MortonDecode(thread_index);
	
	uint sample0_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 0) + 1], 0u, sample0_uint);
	uint sample1_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 1) + 1], 0u, sample1_uint);
	uint sample2_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 2) + 1], 0u, sample2_uint);
	uint sample3_uint; InterlockedExchange(culling_hzb_build_state[(thread_index * 4 + 3) + 1], 0u, sample3_uint);
	
	sample0 = asfloat(sample0_uint);
	sample1 = asfloat(sample1_uint);
	sample2 = asfloat(sample2_uint);
	sample3 = asfloat(sample3_uint);
	GroupDownSample4096x6(thread_id, 0, thread_index, 6, sample0, sample1, sample2, sample3);
	
	if (thread_index == 0) {
		uint original = 0;
		InterlockedExchange(culling_hzb_build_state[0], 0u, original);
	}
}
