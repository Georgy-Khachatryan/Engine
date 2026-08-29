#include "Basic.hlsl"
#include "ColorSpaces.hlsl"
#include "TextureSampling.hlsl"
#include "BrdfSampling.hlsl"

compile_const u32 thread_group_size  = 16;
compile_const float max_frame_count  = 32.0; // Matches blue noise sequence length.
compile_const float blur_frame_count = 8.0;
compile_const float min_blur_radius  = 4.0;  // TODO: This tends to blur out normal map details.
compile_const float max_blur_radius  = 24.0;

#if defined(DISOCCLUSION_MASK)
// Input coordinates the coordinates of the 2x2 pixel quad center.
uint ValidateHistory2x2(float2 sample_coordinates, float3 prev_view_space_position, float n_dot_v, float3 prev_view_space_normal) {
	uint valid_sample_mask_2x2 = 0;
	
	float sample_validity_threshold = 0.005 * prev_view_space_position.z * n_dot_v;
	
	float4 depth_samples = GatherChannel0(depth_stencil_history, sampler_linear_clamp, sample_coordinates * scene.inv_render_target_size);
	for (u32 i = 0; i < 4; i += 1) {
		float sample_depth = depth_samples[i];
		float2 sample_uv = (sample_coordinates + uint2(i & 0x1, i >> 1) - 0.5) * scene.inv_render_target_size;
		
		float3 sample_prev_view_space_position = TransformScreenUvToViewSpace(sample_uv, sample_depth, scene.prev_clip_to_view_coef, scene.prev_jitter_offset_ndc);
		
		bool is_disocclusion =
			abs(dot(prev_view_space_position - sample_prev_view_space_position, prev_view_space_normal)) > sample_validity_threshold ||
			any(sample_uv <  0.0) ||
			any(sample_uv >= 1.0) ||
			sample_depth == 0.0;
		
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
	if (depth == 0.0) {
		denoiser_disocclusion_mask[thread_id] = 0;
		return;
	}
	
	float2 motion_uv_offset  = motion_vectors[thread_id];
	float  motion_z_offset   = depth_motion_vectors[thread_id];
	float2 history_thread_uv = thread_uv + motion_uv_offset;
	float  history_depth     = depth + motion_z_offset;
	float4 normal_roughness  = gb_normal_roughness[thread_id];
	
	float3 view_space_position = TransformScreenUvToViewSpace(history_thread_uv, history_depth, scene.prev_clip_to_view_coef, scene.jitter_offset_ndc);
	float3 world_space_normal  = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	float3 view_space_normal   = mul((float3x3)scene.prev_world_to_view, world_space_normal);
	float  n_dot_v             = saturate(dot(view_space_normal, -normalize(view_space_position)));
	
	float2 history_pixel_coordinates = ComputeBilinearSamplePixelCoordinates(history_thread_uv * scene.render_target_size);
	
	// Validate 4x4 region around history UV coordinates. This is basically 2x2 bilinear region with a 1 pixel border. Samples are in Morton order.
	uint valid_sample_mask_4x4 = 0;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(0.0, 0.0), view_space_position, n_dot_v, view_space_normal) << 0u;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(2.0, 0.0), view_space_position, n_dot_v, view_space_normal) << 4u;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(0.0, 2.0), view_space_position, n_dot_v, view_space_normal) << 8u;
	valid_sample_mask_4x4 |= ValidateHistory2x2(history_pixel_coordinates + float2(2.0, 2.0), view_space_position, n_dot_v, view_space_normal) << 12u;
	
	// Extract 2x2 center region out of the 4x4 valid sample mask. It corresponds to the bilinear filter footprint.
	uint valid_sample_mask_2x2 = 0;
	valid_sample_mask_2x2 |= ((valid_sample_mask_4x4 >> 0x3) & 0x1) << 0u;
	valid_sample_mask_2x2 |= ((valid_sample_mask_4x4 >> 0x6) & 0x1) << 1u;
	valid_sample_mask_2x2 |= ((valid_sample_mask_4x4 >> 0x9) & 0x1) << 2u;
	valid_sample_mask_2x2 |= ((valid_sample_mask_4x4 >> 0xC) & 0x1) << 3u;
	
	uint disocclusion_mask = 0;
	disocclusion_mask |= valid_sample_mask_2x2; // Full bilinear footprint mask.
	disocclusion_mask |= valid_sample_mask_4x4 == 0xFFFF ? 0x10 : 0u; // 1 bit mask for the whole CatmullRom footprint.
	disocclusion_mask |= 0x20; // Valid depth buffer.
	
	denoiser_disocclusion_mask[thread_id] = disocclusion_mask;
}
#endif // defined(DISOCCLUSION_MASK)

#if defined(TEMPORAL_PASS)
[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float2 motion_uv_offset  = motion_vectors[thread_id];
	float2 history_thread_uv = thread_uv + motion_uv_offset;
	
	uint disocclusion_mask = denoiser_disocclusion_mask[thread_id];
	
	float4 bilateral_weights = ComputeBilinearWeights(history_thread_uv * scene.render_target_size) * float4(
		disocclusion_mask & 0x1 ? 1.0 : 0.0,
		disocclusion_mask & 0x2 ? 1.0 : 0.0,
		disocclusion_mask & 0x4 ? 1.0 : 0.0,
		disocclusion_mask & 0x8 ? 1.0 : 0.0
	);
	
	float rcp_weight_sum = all(bilateral_weights == 0.0) ? 0.0 : rcp(bilateral_weights.x + bilateral_weights.y + bilateral_weights.z + bilateral_weights.w);
	
	
	float4 frame_count_samples = GatherChannel0(denoiser_accumulated_frame_count_0, sampler_linear_clamp, history_thread_uv) * 255.0;
	float  history_frame_count = dot(frame_count_samples, bilateral_weights) * rcp_weight_sum;
	float3 history_sample_s = 0.0;
	float3 history_sample_d = 0.0;
	
	if (disocclusion_mask & 0x10) {
		history_sample_s = SampleTextureCatmullRom(denoiser_radiance_history_s_0, sampler_linear_clamp, history_thread_uv, 0.0, scene.render_target_size, scene.inv_render_target_size);
		history_sample_d = SampleTextureCatmullRom(denoiser_radiance_history_d_0, sampler_linear_clamp, history_thread_uv, 0.0, scene.render_target_size, scene.inv_render_target_size);
	} else {
		float3x4 sample_matrix_s = GatherMatrix3x4(denoiser_radiance_history_s_0, float, sampler_linear_clamp, history_thread_uv);
		history_sample_s = mul(sample_matrix_s, bilateral_weights) * rcp_weight_sum;
		
		float3x4 sample_matrix_d = GatherMatrix3x4(denoiser_radiance_history_d_0, float, sampler_linear_clamp, history_thread_uv);
		history_sample_d = mul(sample_matrix_d, bilateral_weights) * rcp_weight_sum;
	}
	
	float4x4 sample_matrix = GatherMatrix4x4(denoiser_variance_0, float, sampler_linear_clamp, history_thread_uv);
	float4 history_moments = mul(sample_matrix, bilateral_weights) * rcp_weight_sum;
	
	float3 history_radiance_s = mul(rec709_to_ycbcr, max(history_sample_s * scene.exposure_history_ratio, 0.0));
	float3 current_radiance_s = mul(rec709_to_ycbcr, denoiser_radiance_source_s[thread_id]);
	
	float3 history_radiance_d = mul(rec709_to_ycbcr, max(history_sample_d * scene.exposure_history_ratio, 0.0));
	float3 current_radiance_d = mul(rec709_to_ycbcr, denoiser_radiance_source_d[thread_id]);
	
	history_moments = max(history_moments * float2(Pow1(scene.exposure_history_ratio), Pow2(scene.exposure_history_ratio)).xyxy, 0.0);
	
	float3 weighted_moments_pow1_s = 0.0;
	float3 weighted_moments_pow2_s = 0.0;
	float3 weighted_moments_pow1_d = 0.0;
	float3 weighted_moments_pow2_d = 0.0;
	float  weight_sum = 0.0;
	
	compile_const s32 radius = 4;
	for (s32 y = -radius; y <= radius; y += 1) {
		for (s32 x = -radius; x <= radius; x += 1) {
			float gaussian_weight = ComputeGaussianWeight(x, y, radius); // TODO: Check if the radiance for this pixel was computed or not.
			
			float3 radiance_s = mul(rec709_to_ycbcr, denoiser_radiance_source_s[thread_id + s32x2(x, y)]);
			float3 radiance_d = mul(rec709_to_ycbcr, denoiser_radiance_source_d[thread_id + s32x2(x, y)]);
			
			weighted_moments_pow1_s += Pow1(radiance_s) * gaussian_weight;
			weighted_moments_pow2_s += Pow2(radiance_s) * gaussian_weight;
			weighted_moments_pow1_d += Pow1(radiance_d) * gaussian_weight;
			weighted_moments_pow2_d += Pow2(radiance_d) * gaussian_weight;
			weight_sum              += gaussian_weight;
		}
	}
	
	float3 moments_pow1_s = weighted_moments_pow1_s * rcp(weight_sum);
	float3 moments_pow2_s = weighted_moments_pow2_s * rcp(weight_sum);
	float3 standard_deviation_s = sqrt(max(moments_pow2_s - Pow2(moments_pow1_s), 0.0));
	
	float3 moments_pow1_d = weighted_moments_pow1_d * rcp(weight_sum);
	float3 moments_pow2_d = weighted_moments_pow2_d * rcp(weight_sum);
	float3 standard_deviation_d = sqrt(max(moments_pow2_d - Pow2(moments_pow1_d), 0.0));
	
	float clamp_aabb_scale = 1.0;
	float3 aabb_min_s = moments_pow1_s - standard_deviation_s * clamp_aabb_scale;
	float3 aabb_max_s = moments_pow1_s + standard_deviation_s * clamp_aabb_scale;
	
	float3 aabb_min_d = moments_pow1_d - standard_deviation_d * clamp_aabb_scale;
	float3 aabb_max_d = moments_pow1_d + standard_deviation_d * clamp_aabb_scale;
	
	history_radiance_s = clamp(history_radiance_s, aabb_min_s, aabb_max_s);
	history_radiance_d = clamp(history_radiance_d, aabb_min_d, aabb_max_d);
	
	float4 current_moments = 0.0;
	current_moments.x = Pow1(current_radiance_s.x);
	current_moments.y = Pow2(current_radiance_s.x);
	current_moments.z = Pow1(current_radiance_d.x);
	current_moments.w = Pow2(current_radiance_d.x);
	
	// Use spatial variance estimate if we don't have enough temporal samples.
	if (history_frame_count < blur_frame_count) {
		float4 spatial_moments = float4(moments_pow1_s.x, moments_pow2_s.x, moments_pow1_d.x, moments_pow2_d.x);
		current_moments = lerp(spatial_moments, current_moments, Pow2(history_frame_count / blur_frame_count));
	}
	
#if 1
	float2 mv_l = motion_vectors[thread_id + s32x2(-1, 0)];
	float2 mv_r = motion_vectors[thread_id + s32x2(+1, 0)];
	float2 mv_t = motion_vectors[thread_id + s32x2(0, -1)];
	float2 mv_b = motion_vectors[thread_id + s32x2(0, +1)];
	
	float divergence = abs((mv_r.x - mv_l.x) * scene.render_target_size.x + (mv_b.y - mv_t.y) * scene.render_target_size.y);
	float divergence_scale = clamp(rcp(divergence * max_frame_count), 0.5, 1.0);
#else
	float divergence_scale = 1.0;
#endif
	
	float accumulation_ratio = 1.0 / (history_frame_count * divergence_scale + 1.0);
	float result_frame_count = min(history_frame_count + 1.0, max_frame_count);
	float3 result_radiance_s = lerp(history_radiance_s, current_radiance_s, accumulation_ratio);
	float3 result_radiance_d = lerp(history_radiance_d, current_radiance_d, accumulation_ratio);
	float4 result_moments    = lerp(history_moments,    current_moments,    accumulation_ratio);
	
	denoiser_radiance_history_s_1[thread_id] = EncodeR9G9B9E5(mul(ycbcr_to_rec709, result_radiance_s));
	denoiser_radiance_history_d_1[thread_id] = EncodeR9G9B9E5(mul(ycbcr_to_rec709, result_radiance_d));
	denoiser_variance_1[thread_id]           = clamp(result_moments, 0.0, float16_max);
	denoiser_accumulated_frame_count_1[thread_id] = result_frame_count / 255.0;
}
#endif // defined(TEMPORAL_PASS)


#if defined(SPATIAL_PASS)

//
// Using tonemapping results in less fireflies, but at the cost of lost energy,
// which ends up producing dark outlines around screen edge disocclusions.
//
#define ENABLE_SPATIAL_FILTER_TONE_MAPPING 0
#if ENABLE_SPATIAL_FILTER_TONE_MAPPING
float3 ToneMap(float3 color) { return log2(max(color + 1.0, 1.0)); }
float3 InverseToneMap(float3 color) { return exp2(color) - 1.0; }
#else // !ENABLE_SPATIAL_FILTER_TONE_MAPPING
float3 ToneMap(float3 color) { return color; }
float3 InverseToneMap(float3 color) { return color; }
#endif // !ENABLE_SPATIAL_FILTER_TONE_MAPPING


[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float4 normal_roughness = gb_normal_roughness[thread_id];
	
	float4 moments = denoiser_variance_1[thread_id];
	float standard_deviation_s = sqrt(max(moments.y - Pow2(moments.x), 0.0));
	float standard_deviation_d = sqrt(max(moments.w - Pow2(moments.z), 0.0));
	
	float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
	float3 world_space_normal  = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	float3 view_space_normal   = mul((float3x3)scene.world_to_view, world_space_normal);
	
	float history_frame_count  = denoiser_accumulated_frame_count_1[thread_id] * 255.0;
	
	float disocclusion_weight = 1.0 - Pow2(saturate((history_frame_count - 1.0) * rcp(blur_frame_count)));
	bool enable_spatial_filtering = true;
	
	float3 average_radiance_s = 0.0;
	float3 average_radiance_d = 0.0;
	float2 weight_sum         = 0.0;
	if (enable_spatial_filtering) {
		s32 radius = (s32)lerp(min_blur_radius, max_blur_radius, disocclusion_weight);
		for (s32 i = -radius; i <= radius; i += 1) {
			s32 x = constants.pass_index == 0 ? 0 : i;
			s32 y = constants.pass_index == 0 ? i : 0;
			
			float2 gaussian_weight = ComputeGaussianWeight(x, y, radius);
			float sample_depth = depth_stencil[thread_id + s32x2(x, y)];
			float4 sample_normal_roughness = gb_normal_roughness[thread_id + s32x2(x, y)];
			float4 sample_moments = denoiser_variance_1[thread_id + s32x2(x, y)];
			
			float2 sample_uv = ((s32x2)thread_id + s32x2(x, y) + 0.5) * scene.inv_render_target_size;
			
			float3 sample_view_space_position = TransformScreenUvToViewSpace(sample_uv, sample_depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
			float3 sample_world_space_normal  = DecodeHemiOctahedralMap01(sample_normal_roughness.xy) * float3(1.0, 1.0, sample_normal_roughness.w * 2.0 - 1.0);
			
			bool is_disocclusion =
				abs(dot(view_space_position - sample_view_space_position, view_space_normal)) > 0.005 * view_space_position.z ||
				dot(sample_world_space_normal, world_space_normal) < 0.975 ||
				any(sample_uv <  0.0) ||
				any(sample_uv >= 1.0) ||
				sample_depth == 0.0;
			
			if (standard_deviation_s > 0.0 || standard_deviation_d > 0.0) {
				float sharpness = 8.0 * saturate(history_frame_count / blur_frame_count);
				gaussian_weight.x *= saturate(exp2(-sharpness * abs(sample_moments.x - moments.x) / max(standard_deviation_s, 1.0 / 1024.0)));
				gaussian_weight.y *= saturate(exp2(-sharpness * abs(sample_moments.z - moments.z) / max(standard_deviation_d, 1.0 / 1024.0)));
			}
			
			if (is_disocclusion == false) {
				average_radiance_s += ToneMap(denoiser_radiance_history_s_1[thread_id + s32x2(x, y)]) * gaussian_weight.x;
				average_radiance_d += ToneMap(denoiser_radiance_history_d_1[thread_id + s32x2(x, y)]) * gaussian_weight.y;
				weight_sum         += gaussian_weight;
			}
		}
	}
	
	float3 sample_s = 0.0;
	float3 sample_d = 0.0;
	
	if (all(weight_sum > 0.0) && enable_spatial_filtering) {
		sample_s = InverseToneMap(average_radiance_s * rcp(weight_sum.x));
		sample_d = InverseToneMap(average_radiance_d * rcp(weight_sum.y));
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
		
		float abs_cos_theta_o = abs(dot(view_space_normal, normalize(view_space_position))); // View vector not negated because of abs.
		
		float2 preintegrated_brdf = SamplePreintegratedBrdfTable(ggx_preintegrated_brdf_lut, abs_cos_theta_o, roughness);
		float3 specular_demodulation = lerp(dielectric_f0, conductor_f0, metalness) * preintegrated_brdf.x + preintegrated_brdf.y;
		
		result_radiance_s *= max(specular_demodulation, 1.0 / 128.0);
		result_radiance_d *= diffuse_albedo;
		
		float3 result_radiance = result_radiance_s + result_radiance_d;
		
		scene_radiance[thread_id] = float4(result_radiance, 1.0);
	}
}
#endif // defined(SPATIAL_PASS)
