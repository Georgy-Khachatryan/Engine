#include "Basic.hlsl"

groupshared uint gs_luminance_histogram[256];
groupshared uint gs_thread_group_exit_index;

compile_const float min_ev = -16.0;
compile_const float max_ev = +16.0;
compile_const float min_luminance = exp2(min_ev);
compile_const float min_cutoff_t = 0.5; // Ignore 50% of the dimmest pixels.
compile_const float max_cutoff_t = 0.9; // Ignore 10% of the brightest pixels.

compile_const u32 thread_group_size = 16;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	gs_luminance_histogram[thread_index] = 0;
	
	GroupMemoryBarrierWithGroupSync();
	
	if (all(thread_id < scene.render_target_size)) {
		float3 radiance_rec709 = scene_radiance[thread_id].xyz;
		float  luminance = dot(radiance_rec709, rec709_luminance_coefficients);
		
		float ev = luminance <= min_luminance ? min_ev : log2(luminance);
		float bucket_index = (ev - min_ev) * (255.0 / (max_ev - min_ev));
		
		uint index = clamp(round(bucket_index), 0, 255);
		InterlockedAdd(gs_luminance_histogram[index], 1u);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	InterlockedAdd(luminance_histogram[thread_index], gs_luminance_histogram[thread_index]);
	
	if (thread_index == 0) {
		InterlockedAdd(luminance_histogram[256], 1u, gs_thread_group_exit_index);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (gs_thread_group_exit_index != constants.last_thread_group_index) return;
	
	GroupMemoryBarrierWithGroupSync();
	
	InterlockedExchange(luminance_histogram[thread_index], 0u, gs_luminance_histogram[thread_index]);
	
	if (thread_index != 0) return;
	
	InterlockedExchange(luminance_histogram[256], 0u, gs_thread_group_exit_index);
	
	
	float pixel_count = scene.render_target_size.x * scene.render_target_size.y;
	s32 min_cutoff_pixel_count = (s32)(pixel_count * min_cutoff_t);
	s32 max_cutoff_pixel_count = (s32)(pixel_count * max_cutoff_t);
	
	float median_luminance = 0.0;
	s32 prefix_sum = 0;
	for (uint i = 0; i < 256; i += 1) {
		s32 bucket_pixel_count = gs_luminance_histogram[i];
		
		s32 clamped_bucket_pixel_count = min(prefix_sum + bucket_pixel_count, max_cutoff_pixel_count) - max(prefix_sum, min_cutoff_pixel_count);
		if (clamped_bucket_pixel_count > 0) {
			float bucket_ev = i * (1.0 / 255.0) * (max_ev - min_ev) + min_ev;
			float bucket_luminance = exp2(bucket_ev);
			
			median_luminance += bucket_luminance * (float)clamped_bucket_pixel_count;
		}
		
		prefix_sum += bucket_pixel_count;
	}
	median_luminance /= (max_cutoff_pixel_count + 1 - min_cutoff_pixel_count);
	
	exposure[0] = 1.0 / median_luminance;
}
