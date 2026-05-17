#include "Basic.hlsl"

compile_const u32 histogram_bucket_count = AutomaticExposureGpuConstants::histogram_bucket_count;
compile_const u32 thread_group_size = 16;

groupshared uint gs_luminance_histogram[histogram_bucket_count];
groupshared uint gs_thread_group_exit_index;


[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	gs_luminance_histogram[thread_index] = 0;
	_Static_assert(histogram_bucket_count == thread_group_size * thread_group_size, "Mismatching thread group size and histogram bucket count.");
	
	GroupMemoryBarrierWithGroupSync();
	
	if (all(thread_id < scene.render_target_size)) {
		float3 pixel_radiance = scene_radiance[thread_id].xyz;
		float pixel_luminance = dot(pixel_radiance, rec709_luminance_coefficients);
		
		float pixel_ev     = pixel_luminance <= constants.histogram_min_luminance ? constants.histogram_min_ev : log2(pixel_luminance);
		float bucket_index = (pixel_ev - constants.histogram_min_ev) * ((histogram_bucket_count - 1) / (constants.histogram_max_ev - constants.histogram_min_ev));
		
		uint index = clamp(round(bucket_index), 0, histogram_bucket_count - 1);
		InterlockedAdd(gs_luminance_histogram[index], 1u);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	InterlockedAdd(luminance_histogram[thread_index], gs_luminance_histogram[thread_index]);
	
	if (thread_index == 0) {
		InterlockedAdd(luminance_histogram[histogram_bucket_count], 1u, gs_thread_group_exit_index);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (gs_thread_group_exit_index != constants.last_thread_group_index) return;
	
	GroupMemoryBarrierWithGroupSync();
	
	uint thread_bucket_pixel_count = 0;
	InterlockedExchange(luminance_histogram[thread_index], 0u, thread_bucket_pixel_count);
	
	float inv_render_target_area = scene.inv_render_target_size.x * scene.inv_render_target_size.y;
	float thread_bucket_weight   = (float)thread_bucket_pixel_count * inv_render_target_area;
	
	gs_luminance_histogram[thread_index] = asuint(thread_bucket_weight);
	luminance_histogram_readback[thread_index] = thread_bucket_weight;
	
	if (thread_index != 0) return;
	
	InterlockedExchange(luminance_histogram[histogram_bucket_count], 0u, gs_thread_group_exit_index);
	
	float median_ev  = 0.0;
	float prefix_sum = 0.0;
	for (uint bucket_index = 0; bucket_index < histogram_bucket_count; bucket_index += 1) {
		float bucket_weight = asfloat(gs_luminance_histogram[bucket_index]);
		
		float clamped_bucket_pixel_count = min(prefix_sum + bucket_weight, constants.histogram_max_cutoff) - max(prefix_sum, constants.histogram_min_cutoff);
		float bucket_ev = bucket_index * (1.0 / (histogram_bucket_count - 1)) * (constants.histogram_max_ev - constants.histogram_min_ev) + constants.histogram_min_ev;
		
		median_ev  += bucket_ev * max(clamped_bucket_pixel_count, 0.0);
		prefix_sum += bucket_weight;
	}
	median_ev /= (constants.histogram_max_cutoff - constants.histogram_min_cutoff);
	
	float old_ev = exposure[0] > 0.0 ? -log2(exposure[0]) : 0.0;
	float new_ev = clamp(median_ev, constants.exposure_min_ev, constants.exposure_max_ev);
	
	// Framerate independent lerp smoothing.
	float final_ev = lerp(old_ev, new_ev, new_ev > old_ev ? constants.exposure_increase_t : constants.exposure_decrease_t);
	
	exposure[0] = exp2(-final_ev);
	luminance_histogram_readback[histogram_bucket_count] = final_ev;
}
