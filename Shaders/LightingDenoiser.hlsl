#include "Basic.hlsl"
#include "ColorSpaces.hlsl"
#include "TextureSampling.hlsl"
#include "BrdfSampling.hlsl"

//
// TODO:
// - Better disocclusion detection.
// - Normal and roughness weights.
// - Experiment with blending in tone mapped space (currently results in dark outlines around shadows).
// - Anti firefly filter.
// - Optimizations (reduce texture sizes, preload textures to LDS, remove redundant view to world transforms, use half floats, etc).
//
// To be finish:
// - Denoise diffuse and specular radiance separately.
// - Demodulate diffuse and specular radiance.
// - Hole filling for disocclusions.
// - Fix blurriness in motion, especially when zooming in. Maybe sharpen history based on MV divergence.
//

compile_const u32 thread_group_size = 16;


#if defined(TEMPORAL_PASS)
// Input coordinates the coordinates of the 2x2 pixel quad center.
uint ValidateHistory2x2(float2 sample_coordinates, float3 world_space_position, float view_space_depth, float3 world_space_normal) {
	uint valid_sample_mask_2x2 = 0;
	
	float4 history_depths = GatherChannel<0>(depth_stencil_history, sampler_linear_clamp, sample_coordinates * scene.inv_render_target_size);
	for (u32 i = 0; i < 4; i += 1) {
		float history_depth = history_depths[i];
		float2 history_uv = (sample_coordinates + uint2(i & 0x1, i >> 1) - 0.5) * scene.inv_render_target_size;
		
		float3 history_view_space_position  = TransformScreenUvToViewSpace(history_uv, history_depth, scene.prev_clip_to_view_coef);
		float3 history_world_space_position = mul(scene.prev_view_to_world, float4(history_view_space_position, 1.0));
		
		bool is_disocclusion = 
			abs(dot(world_space_position - history_world_space_position, world_space_normal)) > 0.005 * view_space_depth ||
			any(history_uv < 0.5 * scene.inv_render_target_size) ||
			any(history_uv > (1.0 - 0.5 * scene.inv_render_target_size)) ||
			history_depth == 0.0;
		
		if (is_disocclusion == false) {
			valid_sample_mask_2x2 |= (1u << i);
		}
	}
	
	return valid_sample_mask_2x2;
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float2 motion_uv_offset  = motion_vectors[thread_id];
	float2 history_thread_uv = thread_uv + motion_uv_offset;
	
	float4 normal_roughness = gb_normal_roughness[thread_id];
	
	float3 view_space_position  = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
	float3 world_space_position = mul(scene.view_to_world, float4(view_space_position, 1.0));
	float3 world_space_normal   = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	
	float2 history_pixel_coordinates = ComputeBilinearSamplePixelCoordinates(history_thread_uv * scene.render_target_size);
	
	// Validate 4x4 region around history UV coordinates. This is basically 2x2 bilinear region with a 1 pixel border. Samples are in Morton order.
	uint valid_sample_mask_4x4 = 0;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(0.0, 0.0), world_space_position, view_space_position.z, world_space_normal) << 0u;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(2.0, 0.0), world_space_position, view_space_position.z, world_space_normal) << 4u;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(0.0, 2.0), world_space_position, view_space_position.z, world_space_normal) << 8u;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(2.0, 2.0), world_space_position, view_space_position.z, world_space_normal) << 12u;
	
	// Extract 2x2 center region out of the 4x4 valid sample mask. It corresponds to the bilinear filter footprint.
	float4 bilateral_weights = ComputeBilinearWeights(history_thread_uv * scene.render_target_size) * float4(
		valid_sample_mask_4x4 & (1u << 0x3) ? 1.0 : 0.0,
		valid_sample_mask_4x4 & (1u << 0x6) ? 1.0 : 0.0,
		valid_sample_mask_4x4 & (1u << 0x9) ? 1.0 : 0.0,
		valid_sample_mask_4x4 & (1u << 0xC) ? 1.0 : 0.0
	);
	
	float rcp_weight_sum = all(bilateral_weights == 0.0) ? 0.0 : rcp(bilateral_weights.x + bilateral_weights.y + bilateral_weights.z + bilateral_weights.w);
	
	
	float4 frame_count_samples = GatherChannel<0>(denoiser_accumulated_frame_count_0, sampler_linear_clamp, history_thread_uv) * 255.0;
	float  history_frame_count = dot(frame_count_samples, bilateral_weights) * rcp_weight_sum;
	float3 history_sample_s = 0.0;
	float3 history_sample_d = 0.0;
	
	if (valid_sample_mask_4x4 == 0xFFFF) {
		history_sample_s = SampleTextureCatmullRom(denoiser_radiance_history_s_0, sampler_linear_clamp, history_thread_uv, 0.0, scene.render_target_size, scene.inv_render_target_size);
		history_sample_d = SampleTextureCatmullRom(denoiser_radiance_history_d_0, sampler_linear_clamp, history_thread_uv, 0.0, scene.render_target_size, scene.inv_render_target_size);
	} else {
		float3x4 sample_matrix_s;
		sample_matrix_s[0] = GatherChannel<0>(denoiser_radiance_history_s_0, sampler_linear_clamp, history_thread_uv);
		sample_matrix_s[1] = GatherChannel<1>(denoiser_radiance_history_s_0, sampler_linear_clamp, history_thread_uv);
		sample_matrix_s[2] = GatherChannel<2>(denoiser_radiance_history_s_0, sampler_linear_clamp, history_thread_uv);
		history_sample_s   = mul(sample_matrix_s, bilateral_weights) * rcp_weight_sum;
		
		float3x4 sample_matrix_d;
		sample_matrix_d[0] = GatherChannel<0>(denoiser_radiance_history_d_0, sampler_linear_clamp, history_thread_uv);
		sample_matrix_d[1] = GatherChannel<1>(denoiser_radiance_history_d_0, sampler_linear_clamp, history_thread_uv);
		sample_matrix_d[2] = GatherChannel<2>(denoiser_radiance_history_d_0, sampler_linear_clamp, history_thread_uv);
		history_sample_d   = mul(sample_matrix_d, bilateral_weights) * rcp_weight_sum;
	}
	
	float3 history_radiance_s = mul(rec709_to_ycbcr, max(history_sample_s * scene.exposure_history_ratio, 0.0));
	float3 current_radiance_s = mul(rec709_to_ycbcr, denoiser_radiance_source_s[thread_id]);
	
	float3 history_radiance_d = mul(rec709_to_ycbcr, max(history_sample_d * scene.exposure_history_ratio, 0.0));
	float3 current_radiance_d = mul(rec709_to_ycbcr, denoiser_radiance_source_d[thread_id]);
	
	float3 weighted_moments_pow1_s = 0.0;
	float3 weighted_moments_pow2_s = 0.0;
	float3 weighted_moments_pow1_d = 0.0;
	float3 weighted_moments_pow2_d = 0.0;
	float  weight_sum = 0.0;
	
	compile_const s32 radius = 4;
	for (s32 y = -radius; y <= radius; y += 1) {
		for (s32 x = -radius; x <= radius; x += 1) {
			float3 radiance_s = denoiser_radiance_source_s[thread_id + s32x2(x, y)];
			float3 radiance_d = denoiser_radiance_source_d[thread_id + s32x2(x, y)];
			float gaussian_weight = ComputeGaussianWeight(x, y, radius); // TODO: Check if the radiance for this pixel was computed or not.
			
			float3 sample_s = mul(rec709_to_ycbcr, radiance_s);
			float3 sample_d = mul(rec709_to_ycbcr, radiance_d);
			weighted_moments_pow1_s += sample_s       * gaussian_weight;
			weighted_moments_pow2_s += Pow2(sample_s) * gaussian_weight;
			weighted_moments_pow1_d += sample_d       * gaussian_weight;
			weighted_moments_pow2_d += Pow2(sample_d) * gaussian_weight;
			weight_sum              += gaussian_weight;
		}
	}
	
	float3 moments_pow1_s = weighted_moments_pow1_s * rcp(weight_sum);
	float3 moments_pow2_s = weighted_moments_pow2_s * rcp(weight_sum);
	float3 standard_deviation_s = sqrt(max(moments_pow2_s - moments_pow1_s * moments_pow1_s, 0.0));
	
	float3 moments_pow1_d = weighted_moments_pow1_d * rcp(weight_sum);
	float3 moments_pow2_d = weighted_moments_pow2_d * rcp(weight_sum);
	float3 standard_deviation_d = sqrt(max(moments_pow2_d - moments_pow1_d * moments_pow1_d, 0.0));
	
	float clamp_aabb_scale = 1.0;
	float3 aabb_min_s = moments_pow1_s - standard_deviation_s * clamp_aabb_scale;
	float3 aabb_max_s = moments_pow1_s + standard_deviation_s * clamp_aabb_scale;
	
	float3 aabb_min_d = moments_pow1_d - standard_deviation_d * clamp_aabb_scale;
	float3 aabb_max_d = moments_pow1_d + standard_deviation_d * clamp_aabb_scale;
	
	history_radiance_s = clamp(history_radiance_s, aabb_min_s, aabb_max_s);
	history_radiance_d = clamp(history_radiance_d, aabb_min_d, aabb_max_d);
	
	current_radiance_s = clamp(current_radiance_s, aabb_min_s, aabb_max_s);
	current_radiance_d = clamp(current_radiance_d, aabb_min_d, aabb_max_d);
	
	compile_const float max_frame_count = 32.0; // Matches blue noise sequence length.
	
#if 1
	float2 mv_l = motion_vectors[thread_id + s32x2(-1, 0)];
	float2 mv_r = motion_vectors[thread_id + s32x2(+1, 0)];
	float2 mv_t = motion_vectors[thread_id + s32x2(0, -1)];
	float2 mv_b = motion_vectors[thread_id + s32x2(0, +1)];
	
	float divergence = abs((mv_r.x - mv_l.x) * scene.render_target_size.x + (mv_b.y - mv_t.y) * scene.render_target_size.y);
	float divergence_scale = clamp(rcp(divergence * max_frame_count), 0.75, 1.0);
#else
	float divergence_scale = 1.0;
#endif
	
	float result_frame_count = min(history_frame_count + 1.0, max_frame_count);
	float3 result_radiance_s = lerp(history_radiance_s, current_radiance_s, 1.0 / (history_frame_count * divergence_scale + 1.0));
	float3 result_radiance_d = lerp(history_radiance_d, current_radiance_d, 1.0 / (history_frame_count * divergence_scale + 1.0));
	
	result_radiance_s = mul(ycbcr_to_rec709, result_radiance_s);
	result_radiance_d = mul(ycbcr_to_rec709, result_radiance_d);
	
	denoiser_radiance_history_s_1[thread_id] = EncodeR9G9B9E5(result_radiance_s);
	denoiser_radiance_history_d_1[thread_id] = EncodeR9G9B9E5(result_radiance_d);
	denoiser_accumulated_frame_count_1[thread_id] = result_frame_count / 255.0;
}
#endif // defined(TEMPORAL_PASS)


#if defined(SPATIAL_PASS)
[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float4 normal_roughness = gb_normal_roughness[thread_id];
	
	float3 view_space_position  = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
	float3 world_space_position = mul(scene.view_to_world, float4(view_space_position, 1.0));
	float3 world_space_normal   = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	
	float history_frame_count = denoiser_accumulated_frame_count_1[thread_id] * 255.0;
	
	float blur_frame_count = 4.0;
	float blur_weight_s = saturate(rcp(blur_frame_count) * (history_frame_count - 1.0));
	float blur_weight_d = saturate(rcp(blur_frame_count) * (history_frame_count - 1.0));
	
	bool enable_spatial_filtering = (blur_weight_s < 1.0) || (blur_weight_d < 1.0);
	
	float3 average_radiance_s = 0.0;
	float3 average_radiance_d = 0.0;
	float  weight_sum         = 0.0;
	if (enable_spatial_filtering) {
		s32 radius = 24.0 * saturate(1.0 - min(blur_weight_s, blur_weight_d));
		for (s32 i = -radius; i <= radius; i += 1) {
			s32 x = constants.pass_index == 0 ? 0 : i;
			s32 y = constants.pass_index == 0 ? i : 0;
			
			float gaussian_weight = ComputeGaussianWeight(x, y, radius);
			float history_depth = depth_stencil[thread_id + s32x2(x, y)];
			float4 history_normal_roughness = gb_normal_roughness[thread_id + s32x2(x, y)];
			
			float2 history_uv = ((s32x2)thread_id + s32x2(x, y) + 0.5) * scene.inv_render_target_size;
			
			float3 history_view_space_position  = TransformScreenUvToViewSpace(history_uv, history_depth, scene.clip_to_view_coef);
			float3 history_world_space_position = mul(scene.view_to_world, float4(history_view_space_position, 1.0));
			float3 history_world_space_normal   = DecodeHemiOctahedralMap01(history_normal_roughness.xy) * float3(1.0, 1.0, history_normal_roughness.w * 2.0 - 1.0);
			
			bool is_disocclusion = 
				abs(dot(world_space_position - history_world_space_position, world_space_normal)) > 0.005 * view_space_position.z ||
				dot(history_world_space_normal, world_space_normal) < 0.975 ||
				any(history_uv <= 0.5 * scene.inv_render_target_size) ||
				any(history_uv >= (1.0 - 0.5 * scene.inv_render_target_size)) ||
				history_depth == 0.0;
			
			if (is_disocclusion == false) {
				average_radiance_s += denoiser_radiance_history_s_1[thread_id + s32x2(x, y)] * gaussian_weight;
				average_radiance_d += denoiser_radiance_history_d_1[thread_id + s32x2(x, y)] * gaussian_weight;
				weight_sum         += gaussian_weight;
			}
		}
	}
	
	float3 sample_s = 0.0;
	float3 sample_d = 0.0;
	
	if (weight_sum > 0.0 && enable_spatial_filtering) {
		average_radiance_s *= rcp(weight_sum);
		average_radiance_d *= rcp(weight_sum);
		
		if (constants.pass_index == 0) {
			sample_s = average_radiance_s;
			sample_d = average_radiance_d;
		} else {
			sample_s = lerp(average_radiance_s, denoiser_radiance_not_blurred_s[thread_id], blur_weight_s);
			sample_d = lerp(average_radiance_d, denoiser_radiance_not_blurred_d[thread_id], blur_weight_d);
		}
	} else if (constants.pass_index == 1) {
		sample_s = denoiser_radiance_not_blurred_s[thread_id];
		sample_d = denoiser_radiance_not_blurred_d[thread_id];
	}
	
	if (enable_spatial_filtering || constants.pass_index == 1) {
		denoiser_radiance_history_s_0[thread_id] = EncodeR9G9B9E5(sample_s);
		denoiser_radiance_history_d_0[thread_id] = EncodeR9G9B9E5(sample_d);
	}
	
	if (constants.pass_index == 1) {
		float3 result_radiance_s = sample_s;
		float3 result_radiance_d = sample_d;
		
		float4 albedo_metalness = gb_albedo_metalness[thread_id];
		
		float  metalness      = albedo_metalness.w;
		float  roughness      = normal_roughness.z;
		float3 conductor_f0   = albedo_metalness.xyz;
		float3 diffuse_albedo = albedo_metalness.xyz;
		
		float abs_cos_theta_o = abs(dot(world_space_normal, -normalize(view_space_position)));
		
		float2 preintegrated_brdf = SampleGgxSingleScatteringEnergyLUT(ggx_preintegrated_brdf_lut, abs_cos_theta_o, roughness);
		float3 specular_demodulation = lerp(dielectric_f0, conductor_f0, metalness) * preintegrated_brdf.x + preintegrated_brdf.y;
		
		result_radiance_s *= max(specular_demodulation, 1.0 / 64.0);
		result_radiance_d *= diffuse_albedo;
		
		float3 result_radiance = result_radiance_s + result_radiance_d;	
		
		scene_radiance[thread_id] = float4(result_radiance, 1.0);
	}
}
#endif // defined(SPATIAL_PASS)
