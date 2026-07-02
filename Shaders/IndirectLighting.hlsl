#include "Basic.hlsl"

#if defined(INDIRECT_DIFFUSE)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "LightEvaluation.hlsl"
#include "GeometrySampling.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

float SampleInverseCDF2x2(uint2 src_tile_id, uint mip_level, float random, inout uint octahedral_index, inout float cdf_weight_sum) {
	octahedral_index *= 4;
	uint2 quad_coordinates = ((src_tile_id * LightingConstants::cdf_tile_size) >> mip_level) + MortonDecode(octahedral_index);
	
	float4 samples; // TODO: Use gather4.
	samples[0] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(0, 0), mip_level));
	samples[1] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(1, 0), mip_level));
	samples[2] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(0, 1), mip_level));
	samples[3] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(1, 1), mip_level));
	
	u32 sample_index = 0;
	float sample_weight = 0.0;
	
	while (sample_index < 4) {
		sample_weight = samples[sample_index];
		if (cdf_weight_sum + sample_weight > random) break;
		
		cdf_weight_sum += sample_weight;
		sample_index   += 1;
	}
	
	octahedral_index += min(sample_index, 3);
	
	return sample_weight;
}

float4 SampleDirectionInverseCDF(uint2 src_tile_id, float3 world_space_normal, float2 diffuse_blue_noise, float random) {
	float cdf_weight_sum = 0.0;
	uint octahedral_index = 0;
	
	SampleInverseCDF2x2(src_tile_id, 3, random, octahedral_index, cdf_weight_sum);
	SampleInverseCDF2x2(src_tile_id, 2, random, octahedral_index, cdf_weight_sum);
	SampleInverseCDF2x2(src_tile_id, 1, random, octahedral_index, cdf_weight_sum);
	float weight = SampleInverseCDF2x2(src_tile_id, 0, random, octahedral_index, cdf_weight_sum);
	_Static_assert(LightingConstants::cdf_mip_count == 4, "CDF inversion assumes 4 MIP levels.");
	
	uint2  octahedral_coords = MortonDecode(octahedral_index);
	float2 octahedral_direction = (octahedral_coords + diffuse_blue_noise) * (1.0 / thread_group_size);
	
	// inv_pdf = (4.0 * PI / (weight * cdf_tile_area)) * (1.0 / PI) * saturate(dot(N, L))
	float3 direction = DecodeOctahedralMap(octahedral_direction * 2.0 - 1.0);
	float inv_pdf = rcp(weight * 64.0) * saturate(dot(world_space_normal, direction));
	_Static_assert(LightingConstants::cdf_tile_size == 16, "CDF inversion assumes 16x16 tiles.");
	
	// Clamp inv_pdf to prevent fireflies.
	return float4(direction, clamp(inv_pdf, 0.0, 16.0));
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) {
		indirect_diffuse[thread_id] = 0;
		return;
	}
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), scene.frame_index);
	
	float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef);
	
	float4 normal_roughness   = gb_normal_roughness[thread_id];
	float3 world_space_normal = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	
	float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
	float3x3 tangent_to_world = transpose(world_to_tangent);
	
	RayDesc ray_desc;
	ray_desc.Origin = mul(scene.view_to_world, float4(view_space_position, 1.0));
	ray_desc.TMin   = 0.0;
	ray_desc.TMax   = 1024.0;
	
	float2 diffuse_blue_noise   = blue_noise_2d[uint3((thread_id + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)];
	float cosine_lobe_mis_noise = blue_noise_1d[uint3((thread_id + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)];
	float2 src_tile_blue_noise  = blue_noise_2d[uint3(thread_id % 128, scene.frame_index % 32)];
	
	float2 motion_uv_offset = motion_vectors[thread_id];
	uint  disocclusion_mask = denoiser_disocclusion_mask[thread_id];
	
	uint2 tile_list_size = scene.indirect_diffuse_cdf_tile_list_size;
	s32x2 src_tile_id = ComputeStochasticBilinearSamplePosition(thread_uv, tile_list_size, motion_uv_offset, src_tile_blue_noise);
	bool src_tile_valid = all(src_tile_id >= 0) && all(src_tile_id < tile_list_size) && (disocclusion_mask & 0xF) != 0;
	
	float inv_pdf = 0.0;
	if (src_tile_valid && cosine_lobe_mis_noise >= 0.05) {
		float random = ComputeRandomUnorm16x2(hash).x;
		float4 direction_and_inv_pdf = SampleDirectionInverseCDF(src_tile_id, world_space_normal, diffuse_blue_noise, random);
		ray_desc.Direction = direction_and_inv_pdf.xyz;
		inv_pdf = direction_and_inv_pdf.w;
	}
	
	if (inv_pdf == 0.0) {
		ray_desc.Direction = mul(tangent_to_world, CosineWeightedHemisphereMapping(diffuse_blue_noise));
		inv_pdf = 1.0;
	}
	
	ray_desc.Origin += world_space_normal * (1.0 / 1024.0);
	
	RayQuery<
		RAY_FLAG_CULL_NON_OPAQUE |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
		// RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
		RAY_FLAG_NONE
	> ray_query;
	
	ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
	
	while (ray_query.Proceed()) {
		
	}
	
	LightAccumulator light_accumulator;
	light_accumulator.radiance = 0.0;
	
	if (ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		MaterialProperties properties = SampleMaterialFromHitResult(
			NvRtGetCommittedClusterID(ray_query),
			ray_query.CommittedInstanceID(),
			ray_query.CommittedPrimitiveIndex(),
			ray_query.CommittedTriangleBarycentrics(),
			ray_query.CommittedTriangleFrontFace(),
			/*texcoord_grad=*/(1.0 / 128.0)
		);
		
		float3 world_space_normal = properties.normal;
		
		ray_desc.Origin += ray_desc.Direction * ray_query.CommittedRayT() + world_space_normal * (1.0 / 1024.0);
		
		float  metalness    = properties.metalness;
		float  roughness    = properties.roughness;
		float3 conductor_f0 = properties.albedo;
		float  alpha        = Pow2(roughness);
		float  alpha_square = Pow2(alpha);
		float3 diffuse_albedo = properties.albedo;
		
		LightSample light_sample = SampleLightWRS(ray_desc.Origin, world_space_normal, ComputeRandomUnorm16x2(hash).x);
		
		if (light_sample.light_entity_index != u32_max) {
			float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
			float3x3 tangent_to_world = transpose(world_to_tangent);
			
			float3 wo = mul(world_to_tangent, -ray_desc.Direction);
			float abs_cos_theta_o = abs(wo.z);
			
			float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
			
			ShadowSampler shadow_sampler;
			shadow_sampler.penumbra_noise = ConcentricMapping(ComputeRandomUnorm16x2(hash));
			
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
				diffuse_albedo,
				/*throughput=*/1.0,
				single_scattering_energy,
				light_sample
			);
			
			// Insert:
			{
				RadianceHashTableKey key = BuildRadianceHashTableKey(ray_desc.Origin, scene.world_space_camera_position, world_space_normal);
				HashTableFindResult result = HashTableAddOrFind(radiance_hash_table_keys, key, LightingConstants::radiance_hash_table_size, LightingConstants::radiance_hash_table_size);
				if (result.is_found) {
					u32 dst_index  = result.hash_index + LightingConstants::radiance_hash_table_size;
					u32 dst_offset = dst_index * sizeof(float16x4);
					
					NvInterlockedAddFp16x2(radiance_hash_table_values, dst_offset + 0, light_accumulator.radiance.xy);
					NvInterlockedAddFp16x2(radiance_hash_table_values, dst_offset + 4, float2(light_accumulator.radiance.z, 1.0));
				}
			}
			
			// Lookup:
			{
				RadianceHashTableKey key = BuildRadianceHashTableKey(ray_desc.Origin, scene.prev_world_space_camera_position, world_space_normal);
				HashTableFindResult result = HashTableFind(radiance_hash_table_keys, key, LightingConstants::radiance_hash_table_size);
				if (result.is_found) {
					u32 src_index  = result.hash_index;
					u32 src_offset = src_index * sizeof(float16x4);
					
					light_accumulator.radiance = radiance_hash_table_values.Load<float16x3>(src_offset);
				}
			}
		}
	} else {
		light_accumulator.radiance = SampleSkyPanoramaLUT(atmosphere, sky_panorama_lut, transmittance_lut, scene.world_space_camera_position, ray_desc.Direction, false);
	}
	
	indirect_diffuse[thread_id] = EncodeR9G9B9E5(light_accumulator.radiance * inv_pdf * scene.exposure_estimate);
	
	float2 octahedral_direction = EncodeOctahedralMap(ray_desc.Direction) * 0.5 + 0.5;
	uint2  octahedral_coords = (uint2)clamp(octahedral_direction * (float)thread_group_size, 0.0, (float)(thread_group_size - 1));
	uint   octahedral_index  = MortonEncode(octahedral_coords);
	
	indirect_diffuse_directions[thread_id] = octahedral_index;
}
#endif // defined(INDIRECT_DIFFUSE)


#if defined(UPDATE_RADIANCE_HASH_TABLE)
#include "Generated/LightData.hlsl"

[ThreadGroupSize(256, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id >= LightingConstants::radiance_hash_table_size) return;
	
	uint src_index = thread_id;
	uint dst_index = thread_id + LightingConstants::radiance_hash_table_size;
	
	float16x4 radiance_and_sample_count = radiance_hash_table_values.Load<float16x4>(dst_index * sizeof(float16x4));
	
	float3 radiance = 0.0;
	if (radiance_and_sample_count.w > 0.0) {
		// Make sure to clamp infinities to float16_max on very bright hash cells. Even if this introduces
		// a hue shift, such bright radiance would almost certainly saturate to white during tone mapping.
		compile_const float float16_max = 65504.0;
		radiance = min((float3)radiance_and_sample_count.xyz, float16_max) / min((float)radiance_and_sample_count.w, float16_max);
	}
	
	u16x4 history_payload = radiance_hash_table_values.Load<u16x4>(src_index * sizeof(float16x4));
	
	float3 history_radiance   = asfloat16(history_payload.xyz);
	u32   history_frame_count = (history_payload.w & 0xFF);
	u32   unused_frame_count  = radiance_and_sample_count.w <= 0.0 ? (history_payload.w >> 8) : 0;
	
	u32   max_frame_count    = 32;
	float accumulation_ratio = 1.0 / (history_frame_count + 1.0);
	
	u16x4 new_history_payload = 0;
	if (radiance_and_sample_count.w > 0) {
		radiance = lerp(history_radiance, radiance, accumulation_ratio);
		u32 result_frame_count = min(history_frame_count + 1, max_frame_count);
		
		new_history_payload.xyz = asuint16(float16x3(radiance));
		new_history_payload.w  |= (u16)(result_frame_count & 0xFF);
	} else {
		new_history_payload.xyz = asuint16(float16x3(history_radiance));
		new_history_payload.w  |= (u16)((history_frame_count & 0xFF));
		new_history_payload.w  |= (u16)((unused_frame_count + 1) << 8);
	}
	bool kill_hash_cell = (unused_frame_count >= 32);
	
	if (kill_hash_cell) {
		new_history_payload = 0;
		radiance_hash_table_keys[src_index] = 0;
		radiance_hash_table_keys[dst_index] = 0;
	} else {
		radiance_hash_table_keys[src_index] = radiance_hash_table_keys[dst_index];
	}
	
	radiance_hash_table_values.Store<u16x4>(dst_index * sizeof(u16x4), u16x4(0u, 0u, 0u, 0u));
	radiance_hash_table_values.Store<u16x4>(src_index * sizeof(u16x4), new_history_payload);
}
#endif// defined(UPDATE_RADIANCE_HASH_TABLE)


#if defined(BUILD_GUIDE_BUFFERS)
compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

groupshared uint gs_directional_weight[thread_group_area];
groupshared uint gs_directional_weight_sum;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	if (thread_index == 0) {
		gs_directional_weight_sum = 0;
	}
	gs_directional_weight[thread_index] = 0;
	
	GroupMemoryBarrierWithGroupSync();
	
	{
		float3 radiance = indirect_diffuse[thread_id];
		uint octahedral_index = indirect_diffuse_directions[thread_id];
		
		float luminance = dot(radiance, rec709_luminance_coefficients);
		float weight = log2(luminance + 1.0);
		
		uint weight_u32 = (u32)(weight * 1024);
		
		InterlockedAdd(gs_directional_weight[octahedral_index], weight_u32);
		InterlockedAdd(gs_directional_weight_sum, weight_u32);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	float rcp_weight_sum = rcp((float)gs_directional_weight_sum);
	u32 mip_0 = gs_directional_weight[thread_index];
	
	indirect_diffuse_tile_cdf[0][thread_id] = mip_0 * rcp_weight_sum;
	
	GroupMemoryBarrierWithGroupSync();
	
	u32 mip_1 = mip_0;
	mip_1 += WaveShuffleXor(mip_1, 0x1);
	mip_1 += WaveShuffleXor(mip_1, 0x2);
	
	if ((thread_index & 0x3) == 0) {
		indirect_diffuse_tile_cdf[1][thread_id / 2] = mip_1 * rcp_weight_sum;
	}
	
	float mip_2 = mip_1;
	mip_2 += WaveShuffleXor(mip_2, 0x4);
	mip_2 += WaveShuffleXor(mip_2, 0x8);
	
	if ((thread_index & 0xF) == 0) {
		indirect_diffuse_tile_cdf[2][thread_id / 4] = mip_2 * rcp_weight_sum;
		gs_directional_weight[thread_index / 16] = mip_2;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index >= 16) return;
	
	thread_id = group_id * 4 + MortonDecode(thread_index);
	
	uint mip_3 = gs_directional_weight[thread_index];
	mip_3 += WaveShuffleXor(mip_3, 0x1);
	mip_3 += WaveShuffleXor(mip_3, 0x2);
	
	if ((thread_index & 0x3) == 0) {
		indirect_diffuse_tile_cdf[3][thread_id / 2] = mip_3 * rcp_weight_sum;
	}
}
#endif // defined(BUILD_GUIDE_BUFFERS)
