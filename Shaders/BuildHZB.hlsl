#include "Basic.hlsl"
#include "ParallelReduction.hlsl"

groupshared float gs_samples[64];
groupshared uint gs_thread_group_exit_index;

struct ParallelReductionSettings {
	static float Reduce(float lh, float rh) { return min(lh, rh); }
	static void StoreResult(u32 mip_index, uint2 coordinates, float sample) { culling_hzb[mip_index][coordinates] = sample; }
	static void StoreGroupShared(u32 index, float sample) { gs_samples[index] = sample; }
	static float LoadGroupShared(u32 index) { return gs_samples[index]; }
};


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
	float sample = GroupDownSample4096x6<ParallelReductionSettings>(thread_id, group_id, thread_index, 0, sample0, sample1, sample2, sample3);
	
	if (thread_index == 0) {
		InterlockedAdd(parallel_reduction_state[0], 1u, gs_thread_group_exit_index);
		
		uint original = 0;
		InterlockedExchange(parallel_reduction_state[MortonEncode(group_id) + 1], asuint(sample), original);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (gs_thread_group_exit_index != constants.last_thread_group_index) return;
	thread_id = MortonDecode(thread_index);
	
	uint sample0_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 0) + 1], 0u, sample0_uint);
	uint sample1_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 1) + 1], 0u, sample1_uint);
	uint sample2_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 2) + 1], 0u, sample2_uint);
	uint sample3_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 3) + 1], 0u, sample3_uint);
	
	sample0 = asfloat(sample0_uint);
	sample1 = asfloat(sample1_uint);
	sample2 = asfloat(sample2_uint);
	sample3 = asfloat(sample3_uint);
	GroupDownSample4096x6<ParallelReductionSettings>(thread_id, 0, thread_index, 6, sample0, sample1, sample2, sample3);
	
	if (thread_index == 0) {
		uint original = 0;
		InterlockedExchange(parallel_reduction_state[0], 0u, original);
	}
}
