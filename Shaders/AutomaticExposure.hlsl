#include "Basic.hlsl"

compile_const u32 histogram_bucket_count = AutomaticExposureGpuConstants::histogram_bucket_count;
compile_const u32 thread_group_size      = AutomaticExposureGpuConstants::thread_group_size;

groupshared uint gs_luminance_histogram[histogram_bucket_count];
groupshared uint gs_thread_group_exit_index;


[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	gs_luminance_histogram[thread_index] = 0;
	_Static_assert(histogram_bucket_count == thread_group_size * thread_group_size, "Mismatching thread group size and histogram bucket count.");
	
	GroupMemoryBarrierWithGroupSync();
	
	for (uint i = 0; i < 16; i += 1) {
		float2 pixel_center_coordinates = (float2)(thread_id * 8u + MortonDecode(i) * 2u + 1u);
		if (any(pixel_center_coordinates >= scene.render_target_size)) continue;
		
		float3 pixel_radiance = scene_radiance.SampleLevel(sampler_linear_clamp, pixel_center_coordinates * scene.inv_render_target_size, 0);
		float pixel_luminance = dot(pixel_radiance, rec709_luminance_coefficients) * scene.inv_exposure_estimate;
		
		// constants.bucket_index_to_ev.y is the same as histogram_min_ev = log2(constants.histogram_min_luminance).
		float pixel_ev     = pixel_luminance <= constants.histogram_min_luminance ? constants.bucket_index_to_ev.y : log2(pixel_luminance);
		float bucket_index = pixel_ev * constants.ev_to_bucket_index.x + constants.ev_to_bucket_index.y;
		
		uint clamped_bucket_index = clamp(round(bucket_index), 0, histogram_bucket_count - 1);
		InterlockedAdd(gs_luminance_histogram[clamped_bucket_index], 4u); // SampleLevel covers 4 pixels.
	}
	_Static_assert(AutomaticExposureGpuConstants::thread_tile_size == 8, "Histogram generation loop assumes 8x8 texels per thread.");
	
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
		
		float clamped_bucket_weight = min(prefix_sum + bucket_weight, constants.histogram_max_cutoff) - max(prefix_sum, constants.histogram_min_cutoff);
		float bucket_ev = bucket_index * constants.bucket_index_to_ev.x + constants.bucket_index_to_ev.y;
		
		median_ev  += bucket_ev * max(clamped_bucket_weight, 0.0);
		prefix_sum += bucket_weight;
	}
	median_ev /= (constants.histogram_max_cutoff - constants.histogram_min_cutoff);
	
	float old_ev = exposure[1];
	float new_ev = clamp(median_ev, constants.exposure_min_ev, constants.exposure_max_ev);
	
	// Framerate independent lerp smoothing.
	float final_ev = lerp(old_ev, new_ev, new_ev > old_ev ? constants.exposure_increase_t : constants.exposure_decrease_t);
	
	float final_exposure = (constants.method == ExposureMethod::Automatic ? exp2(-final_ev) : 1.0) * constants.exposure_scale;
	
	exposure[0] = final_exposure;
	exposure[1] = final_ev;
	exposure_texture[uint2(0, 0)] = final_exposure;
	luminance_histogram_readback[histogram_bucket_count + 0] = final_ev;
	luminance_histogram_readback[histogram_bucket_count + 1] = final_exposure;
}
