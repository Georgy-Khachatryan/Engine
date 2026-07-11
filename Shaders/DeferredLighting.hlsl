#include "Basic.hlsl"

#if defined(DEFERRED_LIGHTING)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "LightEvaluation.hlsl"

struct HashTableShadowSampler {
	float2 penumbra_noise;
	
	float hashed_visibility;
	float hashed_visibility_weight;
	
	float penumbra_mask;
	bool is_shadow_trace_visible;
	
	float EvaluateVisibility(float3 ray_origin, float3 ray_direction, float ray_length) {
		RayQuery<
			RAY_FLAG_CULL_NON_OPAQUE |
			RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
			// RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
			RAY_FLAG_NONE
		> ray_query;
		
		float3x3 world_to_light = BuildOrthonormalBasis(ray_direction);
		float3x3 light_to_world = transpose(world_to_light);
		
		RayDesc ray_desc;
		ray_desc.Origin    = ray_origin;
		ray_desc.Direction = normalize(ray_direction + mul(light_to_world, float3(penumbra_noise * light_penumbra_size, 0.0)));
		ray_desc.TMin      = 0.0;
		ray_desc.TMax      = ray_length;
		
		ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
		
		while (ray_query.Proceed()) {
			
		}
		
		is_shadow_trace_visible = ray_query.CommittedStatus() == COMMITTED_NOTHING;
		penumbra_mask = is_shadow_trace_visible ? 0.0 : ray_query.CommittedRayT() * light_penumbra_size;
		
		return lerp(is_shadow_trace_visible ? 1.0 : 0.0, hashed_visibility, hashed_visibility_weight);
	}
};

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	uint2 tile_list_size = scene.visible_light_tile_list_size;
	
	uint disocclusion_mask = denoiser_disocclusion_mask[thread_id];
	
	float2 motion_uv_offset = motion_vectors[thread_id];
	float2 src_tile_blue_noise = blue_noise_2d[uint3(thread_id % 128, scene.frame_index % 32)];
	
	s32x2 src_tile_id = ComputeStochasticBilinearSamplePosition(thread_uv, tile_list_size, motion_uv_offset, src_tile_blue_noise);
	uint2 dst_tile_id = (thread_id / LightingConstants::visible_light_tile_size);
	
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
	
	float light_sampling_blue_noise = blue_noise_1d[uint3((thread_id + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)];
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
	bool is_shadow_trace_visible = false;
	
	if (light_sample.light_entity_index != u32_max) {
		float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
		
		float2 hash_table_blue_noise = ConcentricMapping(blue_noise_2d[uint3((thread_id + uint2(63, 17) + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)]);
		
		VisibilityHashTableKey key = BuildVisibilityHashTableKey(ray_desc.Origin, light_sample.light_entity_index, scene.prev_world_space_camera_position, world_space_normal, hash_table_blue_noise);
		HashTableFindResult find_result = HashTableFind(visibility_hash_table_keys, key, LightingConstants::visibility_hash_table_size);
		uint dst_index = find_result.hash_index + LightingConstants::visibility_hash_table_size;
		
		float3 wo = mul(world_to_tangent, -ray_desc.Direction);
		float abs_cos_theta_o = abs(wo.z);
		
		float penumbra_size_meters = max(denoiser_penumbra_mask_0.SampleLevel(sampler_linear_clamp, thread_uv + motion_uv_offset, 0), 0.0);
		
		float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
		
		HashTableShadowSampler shadow_sampler;
		shadow_sampler.penumbra_noise = ConcentricMapping(blue_noise_2d[uint3((thread_id + uint2(61, 67) + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)]);
		shadow_sampler.hashed_visibility        = find_result.is_found ? saturate(f16tof32(visibility_hash_table_values[dst_index])) : 0.0;
		shadow_sampler.hashed_visibility_weight = find_result.is_found ? smoothstep(0.6, 1.0, penumbra_size_meters / key.cell_size) : 0.0;
		shadow_sampler.penumbra_mask            = 0.0;
		shadow_sampler.is_shadow_trace_visible  = false;
		
		EvaluateBRDF(
			light_accumulator,
			shadow_sampler,
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
			light_accumulator.specular_radiance /= max(specular_demodulation, 1.0 / 128.0);
		}
		
		// Not multiplying by inv_pdf, the result is less noisy and biased towards the most important light.
		penumbra_mask           = light_sample.light_is_maybe_visible ? shadow_sampler.penumbra_mask : 0.0;
		is_shadow_trace_visible = shadow_sampler.is_shadow_trace_visible;
		
		if (light_sample.light_is_maybe_visible && (penumbra_size_meters > 0.0)) {
			VisibilityHashTableKey key = BuildVisibilityHashTableKey(ray_desc.Origin, light_sample.light_entity_index, scene.world_space_camera_position, world_space_normal, hash_table_blue_noise);
			HashTableFindResult add_result = HashTableAddOrFind(visibility_hash_table_keys, key, LightingConstants::visibility_hash_table_size, LightingConstants::visibility_hash_table_size);
			
			if (add_result.is_found) {
				uint src_index = add_result.hash_index;
				InterlockedAdd(visibility_hash_table_values[src_index], (u32)((shadow_sampler.is_shadow_trace_visible ? 1u : 0) | (1u << 16u)));
			}
		}
	}
	
	// TODO: Denoise indirect diffuse separately.
	light_accumulator.AddDiffuse(indirect_diffuse[thread_id] * scene.inv_exposure_estimate);
	
	uint thread_index_in_tile = (thread_index % LightingConstants::visible_light_tile_area);
	visible_light_tile_list[dst_tile_index * LightingConstants::visible_light_tile_area + thread_index_in_tile] = is_shadow_trace_visible ? light_sample.light_entity_index : u32_max;
	
	denoiser_radiance_source_s[thread_id] = EncodeR9G9B9E5(light_accumulator.specular_radiance * scene.exposure_estimate);
	denoiser_radiance_source_d[thread_id] = EncodeR9G9B9E5(light_accumulator.diffuse_radiance  * scene.exposure_estimate);
	denoiser_penumbra_mask_1[thread_id] = penumbra_mask;
}
#endif // defined(DEFERRED_LIGHTING)


#if defined(BUILD_VISIBLE_LIGHT_TILE_LIST)
#include "Generated/LightData.hlsl"

groupshared u32 gs_light_entity_indices[LightingConstants::visible_light_tile_area];
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
	for (u32 i = WaveGetLaneIndex(); i < LightingConstants::visible_light_tile_area; i += WaveGetLaneCount()) {
		u32 last_index = i != 0 ? gs_light_entity_indices[i - 1] : u32_max;
		u32 curr_index = gs_light_entity_indices[i];
		
		bool is_active = (curr_index != last_index) || (i == 0);
		u32 write_offset = WavePrefixCountBits(is_active) + prefix_sum;
		prefix_sum += WaveActiveCountBits(is_active);
		
		if (is_active) {
			visible_light_tile_list[dst_tile_index * LightingConstants::visible_light_tile_area + write_offset] = curr_index;
		}
	}
}

[ThreadGroupSize(LightingConstants::visible_light_tile_area, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * LightingConstants::visible_light_tile_size + MortonDecode(thread_index);
	
	uint2 tile_list_size = scene.visible_light_tile_list_size;
	
	uint2 dst_tile_id = (thread_id / LightingConstants::visible_light_tile_size);
	uint dst_tile_index = (tile_list_size.x * dst_tile_id.y + dst_tile_id.x) + (scene.frame_index & 0x1 ? 0 : tile_list_size.x * tile_list_size.y);
	
	if (thread_index == 0) {
		gs_light_entity_count = 0;
	}
	gs_light_entity_indices[thread_index] = u32_max;
	
	GroupMemoryBarrierWithGroupSync();
	
	uint disocclusion_mask = denoiser_disocclusion_mask[thread_id];
	uint visible_light_entity_index = disocclusion_mask != 0 ? visible_light_tile_list[dst_tile_index * LightingConstants::visible_light_tile_area + thread_index] : u32_max;
	
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


#if defined(UPDATE_VISIBILITY_HASH_TABLE)
#include "Generated/LightData.hlsl"

[ThreadGroupSize(256, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id >= LightingConstants::visibility_hash_table_size) return;
	
	uint src_index = thread_id;
	uint dst_index = thread_id + LightingConstants::visibility_hash_table_size;
	
	u32 visibility_and_sample_count = visibility_hash_table_values[src_index];
	
	float visibility = 0.0;
	if (visibility_and_sample_count != 0) {
		visibility = (float)(visibility_and_sample_count & 0xFFFF) * rcp((float)(visibility_and_sample_count >> 16u));
	}
	
	u32 history_payload = visibility_hash_table_values[dst_index];
	
	float history_visibility  = f16tof32(history_payload);
	u32   history_frame_count = (history_payload >> 16) & 0xFF;
	u32   unused_frame_count  = visibility_and_sample_count == 0 ? (history_payload >> 24) : 0;
	
	u32   max_frame_count    = 16;
	float accumulation_ratio = 1.0 / (history_frame_count + 1.0);
	
	u32 new_history_payload = 0;
	if (visibility_and_sample_count != 0) {
		visibility = lerp(history_visibility, visibility, accumulation_ratio);
		u32 result_frame_count = min(history_frame_count + 1, max_frame_count);
		
		new_history_payload |= f32tof16(visibility);
		new_history_payload |= ((result_frame_count & 0xFF) << 16);
	} else {
		new_history_payload |= f32tof16(history_visibility);
		new_history_payload |= ((history_frame_count & 0xFF) << 16);
		new_history_payload |= ((unused_frame_count + 1) << 24);
	}
	bool kill_hash_cell = (unused_frame_count >= 16);
	
	if (kill_hash_cell) {
		new_history_payload = 0;
		visibility_hash_table_keys[src_index] = 0;
		visibility_hash_table_keys[dst_index] = 0;
	} else {
		visibility_hash_table_keys[src_index] = visibility_hash_table_keys[dst_index];
	}
	
	visibility_hash_table_values[src_index] = 0u;
	visibility_hash_table_values[dst_index] = new_history_payload;
}
#endif // defined(UPDATE_VISIBILITY_HASH_TABLE)

