#include "Basic.hlsl"

#if defined(DEFERRED_LIGHTING)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "LightEvaluation.hlsl"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	uint2 tile_list_size = scene.visible_light_tile_list_size;
	
	uint disocclusion_mask = denoiser_disocclusion_mask[thread_id];
	
	float2 motion_uv_offset = motion_vectors[thread_id];
	float2 src_tile_blue_noise = ConcentricMapping(blue_noise_2d[uint3(thread_id % 128, scene.frame_index % 32)]);
	
	s32x2 src_tile_id = (s32x2)round((thread_uv + motion_uv_offset) * tile_list_size + (src_tile_blue_noise - 0.5));
	uint2 dst_tile_id = (thread_id / LightCullingConstants::visible_light_tile_size);
	
	uint src_tile_index = (tile_list_size.x * src_tile_id.y + src_tile_id.x) + (scene.frame_index & 0x1 ? tile_list_size.x * tile_list_size.y : 0);
	uint dst_tile_index = (tile_list_size.x * dst_tile_id.y + dst_tile_id.x) + (scene.frame_index & 0x1 ? 0 : tile_list_size.x * tile_list_size.y);
	
	bool src_tile_valid = all(src_tile_id >= 0) && all(src_tile_id < tile_list_size) && (disocclusion_mask & 0xF) != 0;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_position, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, normalize(view_space_position));
	
	float4 albedo_metalness = gb_albedo_metalness[thread_id];
	float4 normal_roughness = gb_normal_roughness[thread_id];
	
	float  metalness    = albedo_metalness.w;
	float  roughness    = normal_roughness.z;
	float3 conductor_f0 = albedo_metalness.xyz;
	float  alpha        = Pow2(roughness);
	float  alpha_square = Pow2(alpha);
	float3 diffuse_albedo = albedo_metalness.xyz;
	
	float3 world_space_normal = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	ray_desc.Origin += world_space_normal * (1.0 / 1024.0);
	
	float light_sampling_blue_noise = blue_noise_1d[uint3(thread_id % 128, scene.frame_index % 32)];
	float min_light_weight = src_tile_valid ? scene.wrs_min_light_weight : 0.0;
	
	LightSample light_sample = SampleLightWRS(
		ray_desc.Origin,
		world_space_normal,
		light_sampling_blue_noise,
		min_light_weight,
		src_tile_valid,
		src_tile_index,
		visible_light_tile_list
	);
	
	SplitLightAccumulator light_accumulator;
	light_accumulator.specular_radiance = 0.0;
	light_accumulator.diffuse_radiance  = 0.0;
	float penumbra_mask = 0.0;
	
	bool demodulate_radiance = true;
	
	if (light_sample.light_entity_index != u32_max) {
		float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
		
		float3 wo = mul(world_to_tangent, -ray_desc.Direction);
		float abs_cos_theta_o = abs(wo.z);
		
		float2 penumbra_noise = ConcentricMapping(blue_noise_2d[uint3((thread_id + uint2(61, 67)) % 128, scene.frame_index % 32)]);
		
		float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
		
		penumbra_mask = EvaluateBRDF<SplitLightAccumulator, RAY_FLAG_NONE>(
			light_accumulator,
			penumbra_noise,
			ray_desc.Origin,
			world_to_tangent,
			wo,
			abs_cos_theta_o,
			metalness,
			roughness,
			alpha_square,
			conductor_f0,
			(demodulate_radiance ? 1.0 : diffuse_albedo),
			/*throughput=*/1.0,
			single_scattering_energy,
			light_sample
		);
		
		if (demodulate_radiance) {
			float2 preintegrated_brdf = SampleGgxSingleScatteringEnergyLUT(ggx_preintegrated_brdf_lut, abs_cos_theta_o, roughness);
			float3 specular_demodulation = lerp(dielectric_f0, conductor_f0, metalness) * preintegrated_brdf.x + preintegrated_brdf.y;
			light_accumulator.specular_radiance /= max(specular_demodulation, 1.0 / 64.0);
		}
	}
	
	uint thread_index_in_tile = (thread_index % LightCullingConstants::visible_light_tile_area);
	
	bool is_light_visible = any(light_accumulator.specular_radiance + light_accumulator.diffuse_radiance > 0.0);
	visible_light_tile_list[dst_tile_index * LightCullingConstants::visible_light_tile_area + thread_index_in_tile] = is_light_visible ? light_sample.light_entity_index : u32_max;
	
	denoiser_radiance_source_s[thread_id] = EncodeR9G9B9E5(light_accumulator.specular_radiance * scene.exposure_estimate);
	denoiser_radiance_source_d[thread_id] = EncodeR9G9B9E5(light_accumulator.diffuse_radiance  * scene.exposure_estimate);
	
	// Not multiplying by inv_pdf, the result is less noisy and biased towards the most important light.
	// denoiser_penumbra_mask_1[thread_id] = light_sample.light_is_maybe_visible ? penumbra_mask * light_sample.inv_pdf : 0.0;
	denoiser_penumbra_mask_1[thread_id] = light_sample.light_is_maybe_visible ? penumbra_mask : 0.0;
}
#endif // defined(DEFERRED_LIGHTING)


#if defined(BUILD_VISIBLE_LIGHT_TILE_LIST)
#include "Generated/LightData.hlsl"

groupshared u32 gs_light_entity_indices[LightCullingConstants::visible_light_tile_area];
groupshared u32 gs_light_entity_count;

void BitonicSortLightEntityIndices(u32 thread_index) {
	u32 light_entity_count = gs_light_entity_count;
	if (light_entity_count == 0) return;
	
	light_entity_count = RoundUpToPowerOf2(light_entity_count);
	
	// Bitonic sort is Based on DirectX-Graphics-Samples, see license in THIRD_PARTY_LICENSES.md
	for (u32 k = 2; k <= light_entity_count; k <<= 1) {
		for (u32 j = k >> 1; j != 0; j >>= 1) {
			uint index_1 = (thread_index & ~(j - 1)) << 1u | (thread_index & (j - 1)) | j;
			uint index_0 = index_1 ^ (k == 2 * j ? k - 1 : j);
			
			uint light_entity_index_0 = gs_light_entity_indices[index_0];
			uint light_entity_index_1 = gs_light_entity_indices[index_1];
			
			if (light_entity_index_1 < light_entity_index_0) {
				gs_light_entity_indices[index_0] = light_entity_index_1;
				gs_light_entity_indices[index_1] = light_entity_index_0;
			}
			
			GroupMemoryBarrierWithGroupSync();
		}
	}
}

void DeduplicateAndWriteVisibleLightTileList(u32 dst_tile_index, u32 thread_index) {
	if (thread_index >= WaveGetLaneCount()) return;
	
	u32 prefix_sum = 0;
	for (u32 i = WaveGetLaneIndex(); i < LightCullingConstants::visible_light_tile_area; i += WaveGetLaneCount()) {
		u32 last_index = i != 0 ? gs_light_entity_indices[i - 1] : u32_max;
		u32 curr_index = gs_light_entity_indices[i];
		
		bool is_active = (curr_index != last_index) || (i == 0);
		u32 write_offset = WavePrefixCountBits(is_active) + prefix_sum;
		prefix_sum += WaveActiveCountBits(is_active);
		
		if (is_active) {
			visible_light_tile_list[dst_tile_index * LightCullingConstants::visible_light_tile_area + write_offset] = curr_index;
		}
	}
}

[ThreadGroupSize(LightCullingConstants::visible_light_tile_area, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * LightCullingConstants::visible_light_tile_size + MortonDecode(thread_index);
	
	uint2 tile_list_size = scene.visible_light_tile_list_size;
	
	uint2 dst_tile_id = (thread_id / LightCullingConstants::visible_light_tile_size);
	uint dst_tile_index = (tile_list_size.x * dst_tile_id.y + dst_tile_id.x) + (scene.frame_index & 0x1 ? 0 : tile_list_size.x * tile_list_size.y);
	
	if (thread_index == 0) {
		gs_light_entity_count = 0;
	}
	gs_light_entity_indices[thread_index] = u32_max;
	
	GroupMemoryBarrierWithGroupSync();
	
	uint disocclusion_mask = denoiser_disocclusion_mask[thread_id];
	uint visible_light_entity_index = disocclusion_mask != 0 ? visible_light_tile_list[dst_tile_index * LightCullingConstants::visible_light_tile_area + thread_index] : u32_max;
	
	// Deduplicate lights within the wave.
	bool is_highest_lane = WaveIsHighestMatchingLane(WaveMatch(visible_light_entity_index));
	
	if (is_highest_lane && visible_light_entity_index != u32_max) {
		u32 light_index = 0;
		InterlockedAdd(gs_light_entity_count, 1, light_index);
		
		gs_light_entity_indices[light_index] = visible_light_entity_index;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	BitonicSortLightEntityIndices(thread_index);
	
	DeduplicateAndWriteVisibleLightTileList(dst_tile_index, thread_index);
}
#endif // defined(BUILD_VISIBLE_LIGHT_TILE_LIST)

