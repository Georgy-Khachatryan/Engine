#include "Basic.hlsl"
#include "ColorSpaces.hlsl"

//
// TODO:
// - Denoise diffuse and specular radiance separately.
// - Demodulate diffuse and specular radiance.
// - Higher quality history reprojection.
// - Better disocclusion detection.
// - Normal and roughness weights.
// - Hole filling for disocclusions.
// - Fix blurriness in motion, especially when zooming in. Maybe sharpen history based on MV divergence.
// - Make sure we use correct exposure for history.
// - Experiment with blending in tone mapped space (currently results in dark outlines around shadows).
//


// An HLSL function for sampling a 2D texture with Catmull-Rom filtering, using 9 texture samples instead of 16.
// The following code is licensed under the MIT license, see license in THIRD_PARTY_LICENSES.md
float4 SampleTextureCatmullRom(Texture2D<float4> src_texture, SamplerState src_sampler, float2 uv, float mip_level, float2 texture_size, float2 inv_texture_size) {
	// We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
	// down the sample location to get the exact center of our "starting" texel. The starting texel will be at
	// location [1, 1] in the grid, where [0, 0] is the top left corner.
	float2 sample_pos = uv * texture_size;
	float2 tex_pos_1  = floor(sample_pos - 0.5) + 0.5;
	
	// Compute the fractional offset from our starting texel to our original sample location, which we'll
	// feed into the Catmull-Rom spline function to get our filter weights.
	float2 f = sample_pos - tex_pos_1;
	
	// Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
	// These equations are pre-expanded based on our knowledge of where the texels will be located,
	// which lets us avoid having to evaluate a piece-wise function.
	float2 w0 = f * (-0.5f + f * (1.0 - 0.5 * f));
	float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);
	
	// Work out weighting factors and sampling offsets that will let us use bilinear filtering to
	// simultaneously evaluate the middle 2 samples from the 4x4 grid.
	float2 w12 = w1 + w2;
	float2 offset_12 = w2 / (w1 + w2);
	
	// Compute the final UV coordinates we'll use for sampling the texture.
	float2 tex_pos_0  = (tex_pos_1 - 1.0) * inv_texture_size;
	float2 tex_pos_3  = (tex_pos_1 + 2.0) * inv_texture_size;
	float2 tex_pos_12 = (tex_pos_1 + offset_12) * inv_texture_size;
	
	float4 result = 0.0;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_0.x,  tex_pos_0.y), mip_level) * w0.x  * w0.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_12.x, tex_pos_0.y), mip_level) * w12.x * w0.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_3.x,  tex_pos_0.y), mip_level) * w3.x  * w0.y;
	
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_0.x,  tex_pos_12.y), mip_level) * w0.x  * w12.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_12.x, tex_pos_12.y), mip_level) * w12.x * w12.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_3.x,  tex_pos_12.y), mip_level) * w3.x  * w12.y;
	
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_0.x,  tex_pos_3.y), mip_level) * w0.x  * w3.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_12.x, tex_pos_3.y), mip_level) * w12.x * w3.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_3.x,  tex_pos_3.y), mip_level) * w3.x  * w3.y;
	
	return result;
}


float ComputeGaussianWeight(float x, float y, float radius) {
	return exp(-3.0 * (float)(Pow2(x) + Pow2(y)) / Pow2(radius + 1.0));
}


compile_const u32 thread_group_size = 16;


[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float2 motion_uv_offset = motion_vectors[thread_id];
	
	float2 history_thread_uv = thread_uv + motion_uv_offset;
	float history_depth = depth_stencil_history.SampleLevel(sampler_nearest_clamp, history_thread_uv, 0);
	
	float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef);
	float3 history_view_space_position = TransformScreenUvToViewSpace(history_thread_uv, history_depth, scene.prev_clip_to_view_coef);
	
	float3 world_space_position = mul(scene.view_to_world, float4(view_space_position, 1.0));
	float3 history_world_space_position = mul(scene.prev_view_to_world, float4(history_view_space_position, 1.0));
	
	bool is_disocclusion = 
		length(world_space_position - history_world_space_position) > 0.1 ||
		any(history_thread_uv < 0.5 * scene.inv_render_target_size) ||
		any(history_thread_uv > (1.0 - 0.5 * scene.inv_render_target_size));
	
	float3 weighted_moments_pow1 = 0.0;
	float3 weighted_moments_pow2 = 0.0;
	float  weight_sum = 0.0;
	
	compile_const s32 radius = 4;
	for (s32 y = -radius; y <= radius; y += 1) {
		for (s32 x = -radius; x <= radius; x += 1) {
			float gaussian_weight = ComputeGaussianWeight(x, y, radius);
			gaussian_weight *= depth_stencil[thread_id + s32x2(x, y)] != 0.0 ? 1.0 : 0.0;
			
			float3 sample = mul(rec709_to_ycbcr, denoiser_radiance_source[thread_id + s32x2(x, y)].xyz);
			weighted_moments_pow1 += sample       * gaussian_weight;
			weighted_moments_pow2 += Pow2(sample) * gaussian_weight;
			weight_sum            += gaussian_weight;
		}
	}
	
	
	float4 history_sample = SampleTextureCatmullRom(denoiser_radiance_history_0, sampler_linear_clamp, history_thread_uv, 0.0, scene.render_target_size, scene.inv_render_target_size);
	// float4 history_sample = denoiser_radiance_history_0.SampleLevel(sampler_linear_clamp, history_thread_uv, 0);
	
	float3 history_radiance = mul(rec709_to_ycbcr, history_sample.xyz);
	float history_frame_count = history_sample.w;
	
	float3 current_radiance = mul(rec709_to_ycbcr, denoiser_radiance_source[thread_id].xyz);
	
	float3 moments_pow1 = weighted_moments_pow1 * rcp(weight_sum);
	float3 moments_pow2 = weighted_moments_pow2 * rcp(weight_sum);
	float3 standard_deviation = sqrt(max(moments_pow2 - moments_pow1 * moments_pow1, 0.0));
	
	float clamp_aabb_scale = 1.0;
	float3 aabb_min = moments_pow1 - standard_deviation * clamp_aabb_scale;
	float3 aabb_max = moments_pow1 + standard_deviation * clamp_aabb_scale;
	
	history_radiance = clamp(history_radiance, aabb_min, aabb_max);
	
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
	
	if (depth != 0.0) {
		scene_radiance[thread_id] = float4(result_radiance, 1.0);
		// scene_radiance[thread_id] = float4(mul(ycbcr_to_rec709, current_radiance), 1.0);
	}
}
