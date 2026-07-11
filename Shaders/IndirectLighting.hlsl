#include "Basic.hlsl"

compile_const float cdf_encoding_scale = 1024.0;
compile_const float inv_cdf_encoding_scale = 1.0 / cdf_encoding_scale;

enum struct CdfTileFlags : u32 {
	None        = 0u,
	KeepAlive   = 1u << 16,
	NeedsUpdate = 1u << 17,
};

#if defined(INDIRECT_DIFFUSE)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "LightEvaluation.hlsl"
#include "GeometrySampling.hlsl"
#include "TextureSampling.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

uint SampleInverseCDF1x2(float p, inout float random) {
	bool commit_sample = (random < p);
	random = commit_sample ? (random / p) : ((random - p) / (1.0 - p));
	return commit_sample ? 0 : 1;
}

float SampleInverseCDF2x2(uint2 src_tile_id, uint mip_level, inout float2 random, inout uint2 octahedral_coordinates) {
	octahedral_coordinates *= 2u;
	uint2 quad_coordinates = ((src_tile_id * LightingConstants::cdf_tile_size) >> mip_level) + octahedral_coordinates;
	
	float4 samples = 0.0; // TODO: Use gather4.
	samples[0] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(0, 0), mip_level));
	samples[1] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(1, 0), mip_level));
	samples[2] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(0, 1), mip_level));
	samples[3] = indirect_diffuse_tile_cdf.Load(uint3(quad_coordinates + uint2(1, 1), mip_level));
	
	u32 sample_index = 0;
	sample_index |= SampleInverseCDF1x2((samples[0] + samples[1]) / (samples[0] + samples[1] + samples[2] + samples[3]), random[1]) << 1u;
	sample_index |= SampleInverseCDF1x2(samples[sample_index] / (samples[sample_index] + samples[sample_index + 1]),     random[0]) << 0u;
	octahedral_coordinates += MortonDecode2x2(sample_index);
	
	return samples[sample_index];
}

float4 SampleDirectionInverseCDF(uint2 src_tile_id, float3 world_space_normal, float2 diffuse_blue_noise) {
	float weight = 0.0;
	uint2 octahedral_coordinates = 0;
	
	[unroll]
	for (s32 i = LightingConstants::cdf_mip_count - 1; i >= 0; i -= 1) {
		weight = SampleInverseCDF2x2(src_tile_id, i, diffuse_blue_noise, octahedral_coordinates);
	}
	
	float2 octahedral_direction = (octahedral_coordinates + diffuse_blue_noise) * (1.0 / LightingConstants::cdf_tile_size);
	float3 direction = DecodeOctahedralMap01(octahedral_direction);
	
	float inv_pdf = 0.0;
	if (weight > 0.0) {
		// tile_cdf_solid_angle is scaled by cdf_tile_area * (1.0 / PI)
		float octahedral_texel_solid_angle = tile_cdf_solid_angle[octahedral_coordinates] * LightingConstants::inv_cdf_tile_area;
		inv_pdf = octahedral_texel_solid_angle * rcp(weight * inv_cdf_encoding_scale) * saturate(dot(world_space_normal, direction));
	}
	
	return float4(direction, inv_pdf);
}


compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

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
	
	ray_desc.Origin += world_space_normal * (1.0 / 1024.0);
	float3 cdf_hash_table_key_origin = ray_desc.Origin;
	
	float inv_pdf = 0.0;
	{
		float2 diffuse_blue_noise = blue_noise_2d[uint3(thread_id % 128, scene.frame_index % 32)];
		
		CdfHashTableKey key = BuildCdfHashTableKey(cdf_hash_table_key_origin, scene.prev_world_space_camera_position, world_space_normal);
		HashTableFindResult result = HashTableFind(cdf_hash_table_keys, key, LightingConstants::cdf_hash_table_size);
		if (result.is_found) {
			uint2 src_tile_id = MortonDecode(result.hash_index);
			
			float4 direction_and_inv_pdf = SampleDirectionInverseCDF(src_tile_id, world_space_normal, diffuse_blue_noise);
			ray_desc.Direction = direction_and_inv_pdf.xyz;
			inv_pdf = direction_and_inv_pdf.w;
		} else {
			ray_desc.Direction = mul(tangent_to_world, CosineWeightedHemisphereMapping(diffuse_blue_noise));
			inv_pdf = 1.0;
		}
	}
	
	
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
	light_accumulator.radiance *= inv_pdf;
	
	indirect_diffuse[thread_id] = EncodeR9G9B9E5(light_accumulator.radiance * scene.exposure_estimate);
	
	{
		CdfHashTableKey key = BuildCdfHashTableKey(cdf_hash_table_key_origin, scene.world_space_camera_position, world_space_normal);
		HashTableFindResult result = HashTableAddOrFind(cdf_hash_table_keys, key, LightingConstants::cdf_hash_table_size, LightingConstants::cdf_hash_table_size);
		if (result.is_found) {
			float weight = dot(light_accumulator.radiance, rec709_luminance_coefficients);
			
			float2 octahedral_direction = EncodeOctahedralMap01(ray_desc.Direction);
			float2 coordinates = (MortonDecode(result.hash_index) + octahedral_direction) * LightingConstants::cdf_tile_size;
			
			float4 bilinear_weights = ComputeBilinearWeights(coordinates);
			uint2 pixel_coordinates = (uint2)ComputeBilinearSamplePixelCoordinates(coordinates);
			
			[unroll]
			for (u32 i = 0; i < 4; i += 1) {
				uint2 output_coordinates = (pixel_coordinates + MortonDecode2x2(i));
				uint weight_u32 = (u32)(weight * bilinear_weights[i] * 1024.0);
				
				InterlockedAdd(indirect_diffuse_directions[output_coordinates], weight_u32);
			}
			
			uint4 match_mask = WaveMatch(result.hash_index);
			if (WaveIsHighestMatchingLane(match_mask)) {
				InterlockedOr(cdf_hash_table_values[result.hash_index], CdfTileFlags::KeepAlive);
			}
		}
	}
	
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
#endif // defined(UPDATE_RADIANCE_HASH_TABLE)


#if defined(UPDATE_CDF_HASH_TABLE)
#include "Generated/LightData.hlsl"

[ThreadGroupSize(256, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id >= LightingConstants::cdf_hash_table_size) return;
	
	uint src_index = thread_id;
	uint dst_index = thread_id + LightingConstants::cdf_hash_table_size;
	
	u32 history_payload = cdf_hash_table_values[src_index];
	
	u32 history_frame_count = (history_payload & 0xFF);
	u32 unused_frame_count  = (history_payload & CdfTileFlags::KeepAlive) == 0 ? (history_payload >> 8) & 0xFF : 0;
	
	u32 max_frame_count = 32;
	
	u32 new_history_payload = 0;
	if (history_payload & CdfTileFlags::KeepAlive) {
		u32 result_frame_count = min(history_frame_count + 1, max_frame_count);
		new_history_payload |= (result_frame_count & 0xFF);
		new_history_payload |= CdfTileFlags::NeedsUpdate;
	} else {
		new_history_payload |= (history_frame_count & 0xFF);
		new_history_payload |= ((unused_frame_count + 1) << 8);
	}
	bool kill_hash_cell = (unused_frame_count >= 8);
	
	if (kill_hash_cell) {
		new_history_payload = 0;
		cdf_hash_table_keys[src_index] = 0;
		cdf_hash_table_keys[dst_index] = 0;
	} else {
		cdf_hash_table_keys[src_index] = cdf_hash_table_keys[dst_index];
	}
	
	cdf_hash_table_values[src_index] = new_history_payload;
}
#endif // defined(UPDATE_CDF_HASH_TABLE)


#if defined(INDIRECT_DIFFUSE_TILE_CDF)
#include "Generated/LightData.hlsl"
#include "TextureSampling.hlsl"

compile_const u32 thread_group_size = LightingConstants::cdf_tile_size;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

groupshared uint gs_directional_weight[thread_group_area];
groupshared uint gs_directional_weight_sum;

[ThreadGroupSize(thread_group_area, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	uint hash_index = MortonEncode(group_id);
	uint history_payload = cdf_hash_table_values[hash_index];
	
	if ((history_payload & CdfTileFlags::NeedsUpdate) == 0) return;
	
	if (thread_index == 0) {
		gs_directional_weight_sum = 0;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	{
		uint weight_u32 = indirect_diffuse_directions[thread_id];
		indirect_diffuse_directions[thread_id] = 0;
		
		gs_directional_weight[thread_index] = weight_u32;
		InterlockedAdd(gs_directional_weight_sum, weight_u32);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	{
		compile_const uint denominator = thread_group_area * 16;
		uint weight_u32 = max((gs_directional_weight_sum + denominator - 1) / denominator, 1u);
		
		GroupMemoryBarrierWithGroupSync();
		
		InterlockedAdd(gs_directional_weight[thread_index], weight_u32);
		InterlockedAdd(gs_directional_weight_sum, weight_u32);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	float rcp_weight_sum = gs_directional_weight_sum != 0 ? rcp((float)gs_directional_weight_sum) : 0.0;
	float new_mip_0 = gs_directional_weight[thread_index] * rcp_weight_sum * cdf_encoding_scale;
	float old_mip_0 = indirect_diffuse_tile_cdf[0][thread_id];
	
	compile_const u32 initial_estimate_frame_count = 4u;
	
	// Initial estimate of the distribution:
	u32 history_frame_count = (history_payload & 0xFF);
	if (history_frame_count < 2) {
		// TODO: Can check nearby cells, or cells one level above or below.
#define USE_CDF_COSINE_WEIGHTED_HEMISPHERICAL_DISTRIBUTION_INITIAL_ESTIMATE 0
#if USE_CDF_COSINE_WEIGHTED_HEMISPHERICAL_DISTRIBUTION_INITIAL_ESTIMATE
		u64 key = cdf_hash_table_keys[hash_index];
		uint2 encoded_cell_normal = uint2((uint)(key >> 58), (uint)(key >> 61)) & 0x7;
		_Static_assert(CdfHashTableKey::normal_bit_count == 3, "Incorrect CdfHashTableKey normal bit count.");
		
		// Start accumulation from a cosine weighted hemispherical distribution:
		float3 texel_direction = DecodeOctahedralMap01((MortonDecode(thread_index) + 0.5) * (1.0 / LightingConstants::cdf_tile_size));
		float3 cell_normal     = DecodeOctahedralMap01((encoded_cell_normal + 0.5) * (1.0 / 8.0));
		old_mip_0 = saturate(dot(texel_direction, cell_normal)) * 4.0 * LightingConstants::inv_cdf_tile_area * cdf_encoding_scale;
#else // !USE_CDF_COSINE_WEIGHTED_HEMISPHERICAL_DISTRIBUTION_INITIAL_ESTIMATE
		// Start accumulation from a uniform spherical distribution:
		old_mip_0 = LightingConstants::inv_cdf_tile_area * cdf_encoding_scale;
#endif // !USE_CDF_COSINE_WEIGHTED_HEMISPHERICAL_DISTRIBUTION_INITIAL_ESTIMATE
	}
	
	float accumulation_ratio = 1.0 / max(history_frame_count, initial_estimate_frame_count);
	float mip_0 = lerp(old_mip_0, new_mip_0, accumulation_ratio);
	indirect_diffuse_tile_cdf[0][thread_id] = mip_0;
	
	GroupMemoryBarrierWithGroupSync();
	
	float mip_1 = mip_0;
	mip_1 += WaveShuffleXor(mip_1, 0x1);
	mip_1 += WaveShuffleXor(mip_1, 0x2);
	
	if ((thread_index & 0x3) == 0) {
		indirect_diffuse_tile_cdf[1][thread_id / 2] = mip_1;
	}
	
	float mip_2 = mip_1;
	mip_2 += WaveShuffleXor(mip_2, 0x4);
	mip_2 += WaveShuffleXor(mip_2, 0x8);
	
	if ((thread_index & 0xF) == 0) {
		indirect_diffuse_tile_cdf[2][thread_id / 4] = mip_2;
		gs_directional_weight[thread_index / 16] = asuint(mip_2);
	}
	
	if (LightingConstants::cdf_mip_count < 4) return;
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index >= (thread_group_area >> 4u)) return;
	
	thread_id = group_id * (thread_group_size >> 2) + MortonDecode(thread_index);
	
	float mip_3 = asfloat(gs_directional_weight[thread_index]);
	mip_3 += WaveShuffleXor(mip_3, 0x1);
	mip_3 += WaveShuffleXor(mip_3, 0x2);
	
	if ((thread_index & 0x3) == 0) {
		indirect_diffuse_tile_cdf[3][thread_id / 2] = mip_3;
	}
	
	if (LightingConstants::cdf_mip_count < 5) return;
	
	float mip_4 = mip_3;
	mip_4 += WaveShuffleXor(mip_4, 0x4);
	mip_4 += WaveShuffleXor(mip_4, 0x8);
	
	if ((thread_index & 0xF) == 0) {
		indirect_diffuse_tile_cdf[4][thread_id / 4] = mip_4;
	}
}
#endif // defined(INDIRECT_DIFFUSE_TILE_CDF)
