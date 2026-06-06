#include "Basic.hlsl"
#include "ColorSpaces.hlsl"
#include "TextureSampling.hlsl"

//
// TODO:
// - Denoise diffuse and specular radiance separately.
// - Demodulate diffuse and specular radiance.
// - Higher quality history reprojection.
// - Better disocclusion detection.
// - Normal and roughness weights.
// - Hole filling for disocclusions.
// - Fix blurriness in motion, especially when zooming in. Maybe sharpen history based on MV divergence.
// - Experiment with blending in tone mapped space (currently results in dark outlines around shadows).
// - Anti firefly filter.
//

compile_const u32 thread_group_size = 16;


[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float2 motion_uv_offset = motion_vectors[thread_id];
	
	float2 history_thread_uv = thread_uv + motion_uv_offset;
	float4 history_depths    = GatherChannel<0>(depth_stencil_history, sampler_linear_clamp, history_thread_uv);
	float4 bilateral_weights = 1.0;
	
	float3 view_space_position  = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef);
	float3 world_space_position = mul(scene.view_to_world, float4(view_space_position, 1.0));
	
	float2 history_sample_pixel_coordinates = ComputeBilinearSamplePixelCoordinates(history_thread_uv * scene.render_target_size);
	for (u32 i = 0; i < 4; i += 1) {
		float history_depth = history_depths[i];
		float2 history_uv = (history_sample_pixel_coordinates + uint2(i & 0x1, i >> 1)) * scene.inv_render_target_size;
		
		float3 history_view_space_position = TransformScreenUvToViewSpace(history_uv, history_depth, scene.prev_clip_to_view_coef);
		float3 history_world_space_position = mul(scene.prev_view_to_world, float4(history_view_space_position, 1.0));
		
		bool is_disocclusion = 
			length(world_space_position - history_world_space_position) > 0.02 * history_view_space_position.z ||
			any(history_uv < 0.5 * scene.inv_render_target_size) ||
			any(history_uv > (1.0 - 0.5 * scene.inv_render_target_size)) ||
			depth == 0.0;
		
		bilateral_weights[i] = is_disocclusion ? 0.0 : 1.0;
	}
	bilateral_weights *= ComputeBilinearWeights(history_thread_uv * scene.render_target_size);
	
	bool is_disocclusion = all(bilateral_weights == 0.0);
	float rcp_weight_sum = is_disocclusion ? 0.0 : rcp(bilateral_weights.x + bilateral_weights.y + bilateral_weights.z + bilateral_weights.w);
	
	float4 frame_count_samples = GatherChannel<3>(denoiser_radiance_history_0, sampler_linear_clamp, history_thread_uv);
	float  history_frame_count = dot(frame_count_samples, bilateral_weights) * rcp_weight_sum;
	float3 history_sample = 0.0;
	
	if (all(bilateral_weights != 0.0)) {
		history_sample = SampleTextureCatmullRom(denoiser_radiance_history_0, sampler_linear_clamp, history_thread_uv, 0.0, scene.render_target_size, scene.inv_render_target_size).xyz;
	} else {
		float3x4 sample_matrix;
		sample_matrix[0] = GatherChannel<0>(denoiser_radiance_history_0, sampler_linear_clamp, history_thread_uv);
		sample_matrix[1] = GatherChannel<1>(denoiser_radiance_history_0, sampler_linear_clamp, history_thread_uv);
		sample_matrix[2] = GatherChannel<2>(denoiser_radiance_history_0, sampler_linear_clamp, history_thread_uv);
		history_sample   = mul(sample_matrix, bilateral_weights) * rcp_weight_sum;
	}
	
	float3 history_radiance = mul(rec709_to_ycbcr, max(history_sample * scene.exposure_history_ratio, 0.0));
	float3 current_radiance = mul(rec709_to_ycbcr, denoiser_radiance_source[thread_id].xyz);
	
	
	float3 weighted_moments_pow1 = 0.0;
	float3 weighted_moments_pow2 = 0.0;
	float  weight_sum = 0.0;
	
	compile_const s32 radius = 4;
	for (s32 y = -radius; y <= radius; y += 1) {
		for (s32 x = -radius; x <= radius; x += 1) {
			float4 radiance = denoiser_radiance_source[thread_id + s32x2(x, y)];
			float gaussian_weight = ComputeGaussianWeight(x, y, radius) * radiance.w;
			
			float3 sample = mul(rec709_to_ycbcr, radiance.xyz);
			weighted_moments_pow1 += sample       * gaussian_weight;
			weighted_moments_pow2 += Pow2(sample) * gaussian_weight;
			weight_sum            += gaussian_weight;
		}
	}
	
	float3 moments_pow1 = weighted_moments_pow1 * rcp(weight_sum);
	float3 moments_pow2 = weighted_moments_pow2 * rcp(weight_sum);
	float3 standard_deviation = sqrt(max(moments_pow2 - moments_pow1 * moments_pow1, 0.0));
	
	float clamp_aabb_scale = 1.0;
	float3 aabb_min = moments_pow1 - standard_deviation * clamp_aabb_scale;
	float3 aabb_max = moments_pow1 + standard_deviation * clamp_aabb_scale;
	
	history_radiance = clamp(history_radiance, aabb_min, aabb_max);
	// current_radiance = clamp(current_radiance, aabb_min, aabb_max);
	
	
	compile_const float max_frame_count = 32.0; // Matches blue noise sequence length.
	
	float  result_frame_count = 0.0;
	float3 result_radiance    = 0.0;
	if (history_frame_count >= max_frame_count) {
		result_frame_count = history_frame_count;
		result_radiance    = lerp(history_radiance, current_radiance, 0.02);
	} else {
		result_frame_count = history_frame_count + 1.0;
		result_radiance    = (history_radiance * history_frame_count + current_radiance) * rcp(result_frame_count);
	}
	
	if (is_disocclusion) {
		result_radiance = current_radiance;
		result_frame_count = 1.0;
	}
	
	result_radiance = mul(ycbcr_to_rec709, result_radiance);
	
	denoiser_radiance_history_1[thread_id] = float4(result_radiance, result_frame_count);
	
	scene_radiance[thread_id] = float4(result_radiance, 1.0);
}
