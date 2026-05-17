#include "Basic.hlsl"

groupshared uint gs_luminance_histogram[256];
groupshared uint gs_thread_group_exit_index;

compile_const u32 thread_group_size = 16;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	gs_luminance_histogram[thread_index] = 0;
	
	GroupMemoryBarrierWithGroupSync();
	
	if (all(thread_id < scene.render_target_size)) {
		float3 pixel_radiance = scene_radiance[thread_id].xyz;
		float pixel_luminance = dot(pixel_radiance, rec709_luminance_coefficients);
		
		float pixel_ev     = pixel_luminance <= constants.histogram_min_luminance ? constants.histogram_min_ev : log2(pixel_luminance);
		float bucket_index = (pixel_ev - constants.histogram_min_ev) * (255.0 / (constants.histogram_max_ev - constants.histogram_min_ev));
		
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
	s32 min_cutoff_pixel_count = (s32)(pixel_count * constants.histogram_min_cutoff);
	s32 max_cutoff_pixel_count = (s32)(pixel_count * constants.histogram_max_cutoff);
	
	float median_ev = 0.0;
	s32 prefix_sum = 0;
	for (uint i = 0; i < 256; i += 1) {
		s32 bucket_pixel_count = gs_luminance_histogram[i];
		
		s32 clamped_bucket_pixel_count = min(prefix_sum + bucket_pixel_count, max_cutoff_pixel_count) - max(prefix_sum, min_cutoff_pixel_count);
		if (clamped_bucket_pixel_count > 0) {
			float bucket_ev = i * (1.0 / 255.0) * (constants.histogram_max_ev - constants.histogram_min_ev) + constants.histogram_min_ev;
			median_ev += bucket_ev * (float)clamped_bucket_pixel_count;
		}
		
		prefix_sum += bucket_pixel_count;
	}
	median_ev /= (max_cutoff_pixel_count - min_cutoff_pixel_count);
	
	float old_ev = exposure[0] > 0.0 ? -log2(exposure[0]) : 0.0;
	float new_ev = clamp(median_ev, constants.exposure_min_ev, constants.exposure_max_ev);
	
	// Framerate independent lerp smoothing.
	float final_ev = lerp(old_ev, new_ev, new_ev > old_ev ? constants.exposure_increase_t : constants.exposure_decrease_t);
	
	exposure[0] = exp2(-final_ev);
}
