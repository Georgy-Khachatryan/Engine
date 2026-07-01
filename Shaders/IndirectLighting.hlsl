#include "Basic.hlsl"

#if defined(INDIRECT_DIFFUSE)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "LightEvaluation.hlsl"
#include "GeometrySampling.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

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
	
	float2 diffuse_blue_noise = blue_noise_2d[uint3((thread_id + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)];
	
	RayDesc ray_desc;
	ray_desc.Origin = mul(scene.view_to_world, float4(view_space_position, 1.0));
	ray_desc.TMin   = 0.0;
	ray_desc.TMax   = 1024.0;
	ray_desc.Direction = mul(tangent_to_world, CosineWeightedHemisphereMapping(diffuse_blue_noise));
	
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
			ray_query.CommittedTriangleFrontFace()
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
	
	indirect_diffuse[thread_id] = EncodeR9G9B9E5(light_accumulator.radiance * scene.exposure_estimate);
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
