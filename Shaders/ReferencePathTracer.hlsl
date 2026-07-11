#include "Basic.hlsl"

#if defined(REFERENCE_PATH_TRACER)
#include "LightEvaluation.hlsl"
#include "GeometrySampling.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

[ThreadGroupSize(32, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * uint2(8, 4) + MortonDecode(thread_index);
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), scene.path_tracer_accumulated_frame_count);
	float2 thread_uv = (thread_id + ComputeRandomUnorm16x2(hash)) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = 1024.0;
	
	LightAccumulator light_accumulator;
	light_accumulator.radiance = 0.0;
	float3 throughput = 1.0;
	uint max_path_length = 512 + 2;
	
	[loop]
	for (uint i = 0; i < max_path_length; i += 1) {
		RayQuery<
			RAY_FLAG_CULL_NON_OPAQUE |
			RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
			// RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
			RAY_FLAG_NONE
		> ray_query;
		
		ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
		
		while (ray_query.Proceed()) {
			
		}
		
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
			
			// TODO: WRS is slower, but converges much faster. We can make sampling strategy switchable at runtime.
			// LightSample light_sample = SampleLightWRS(ray_desc.Origin, world_space_normal, ComputeRandomUnorm16x2(hash).x);
			LightSample light_sample = SampleLightUniform(ray_desc.Origin, hash);
			
			float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
			float3x3 tangent_to_world = transpose(world_to_tangent);
			
			float3 wo = mul(world_to_tangent, -ray_desc.Direction);
			float abs_cos_theta_o = abs(wo.z);
			
			float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
			
			if (light_sample.light_entity_index != u32_max) {
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
					throughput,
					single_scattering_energy,
					light_sample
				);
			}
			
			BrdfSampleResult brdf_sample = SampleBRDF(
				wo,
				abs_cos_theta_o,
				metalness,
				alpha,
				alpha_square,
				conductor_f0,
				diffuse_albedo,
				single_scattering_energy,
				hash
			);
			
			if (brdf_sample.is_valid) {
				ray_desc.Direction = mul(tangent_to_world, brdf_sample.wi);
				throughput *= brdf_sample.throughput;
			} else {
				i = max_path_length;
			}
		} else {
			float3 sky_radiance = SampleSkyPanoramaLUT(atmosphere, sky_panorama_lut, transmittance_lut, scene.world_space_camera_position, ray_desc.Direction, i == 0);
			
			light_accumulator.radiance += throughput * sky_radiance;
			i = max_path_length;
		}
	}
	
	float3 old_accumulated_radiance = max(path_tracer_radiance[thread_id].xyz, 0.0);
	float3 new_accumulated_radiance = (old_accumulated_radiance * (scene.path_tracer_accumulated_frame_count - 1) + light_accumulator.radiance) / (float)scene.path_tracer_accumulated_frame_count;
	path_tracer_radiance[thread_id] = float4(new_accumulated_radiance, 1.0);
	
	uint reference_path_tracer_min_x = (uint)(scene.render_target_size.x * scene.reference_path_tracer_percent);
	if (thread_id.x < reference_path_tracer_min_x) {
		scene_radiance[thread_id] = float4(new_accumulated_radiance * scene.exposure_estimate, 1.0);
	}
}
#endif // defined(REFERENCE_PATH_TRACER)


#if defined(ENERGY_COMPENSATION_LUT)
#include "BrdfSampling.hlsl"
#include "Generated/LightData.hlsl"

compile_const u32 thread_group_size   = 32;
compile_const u32 thread_group_area   = thread_group_size * thread_group_size;
compile_const u32 sample_grid_size_xy = 128;
compile_const u32 sample_count        = sample_grid_size_xy * sample_grid_size_xy;
compile_const u32 samples_per_thread  = sample_count / thread_group_area;
compile_const u32 min_wave_size       = 16;

groupshared float4 gs_single_scattering_energy[thread_group_area / min_wave_size];

[ThreadGroupSize(thread_group_area, 1, 1)][WaveSize(min_wave_size, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	float2 group_uv = group_id * (1.0 / (energy_compensation_lut_size - 1));
	
	float roughness    = group_uv.y;
	float alpha        = Pow2(roughness);
	float alpha_square = Pow2(alpha);
	float cos_theta    = saturate(group_uv.x + energy_compensation_lut_cos_theta_bias);
	
	float3 wo = float3(sqrt(1.0 - Pow2(cos_theta)), 0.0, cos_theta);
	
	float4 single_scattering_energy = 0.0;
	for (u32 i = 0; i < samples_per_thread; i += 1) {
		u32 sample_index = thread_index * samples_per_thread + i;
		
		// Sample UV have exclusive upper bound, i.e. the range is [0, 1).
		float2 sample_uv = uint2(sample_index % sample_grid_size_xy, sample_index / sample_grid_size_xy) * (1.0 / sample_grid_size_xy);
		
		float3 wh = SampleTrowbridgeReitzVNDF(sample_uv, wo, alpha);
		float3 wi = reflect(-wo, wh);
		
		if ((wi.z * wo.z) > 0.0) {
			float specular_sample = SmithVisibilityG(wo.z, wi.z, alpha_square) / SmithVisibilityG1(wo.z, alpha_square);
			single_scattering_energy.x += specular_sample; // Conductor
			
			float fresnel = FresnelDielectric(dielectric_f0, dot(wo, wh));
			single_scattering_energy.y += fresnel * specular_sample + (1.0 - fresnel); // Dielectric
			
			float fresnel_zero_reflectance = FresnelSchlick(0.0, dot(wo, wh));
			single_scattering_energy.z += specular_sample * (1.0 - fresnel_zero_reflectance);
			single_scattering_energy.w += specular_sample * fresnel_zero_reflectance;
		}
	}
	
	float4 wave_single_scattering_energy = WaveActiveSum(single_scattering_energy);
	if (WaveIsFirstLane()) {
		gs_single_scattering_energy[thread_index / WaveGetLaneCount()] = wave_single_scattering_energy;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		float4 group_single_scattering_energy = wave_single_scattering_energy;
		
		u32 wave_count = thread_group_area / WaveGetLaneCount();
		for (u32 i = 1; i < wave_count; i += 1) {
			group_single_scattering_energy += gs_single_scattering_energy[i];
		}
		
		ggx_single_scattering_energy_lut[group_id] = group_single_scattering_energy.xy / sample_count;
		ggx_preintegrated_brdf_lut[group_id]       = group_single_scattering_energy.zw / sample_count;
	}
	
	uint2 thread_id = group_id  * thread_group_size + MortonDecode(thread_index);
	if (all(thread_id < LightingConstants::cdf_tile_size)) {
		float4 sample_rect = (uint4(thread_id, thread_id + 1u) * (1.0 / LightingConstants::cdf_tile_size)) * 2.0 - 1.0;
		
		float3 n0 = DecodeOctahedralMap(sample_rect.xy);
		float3 n1 = DecodeOctahedralMap(sample_rect.zy);
		float3 n2 = DecodeOctahedralMap(sample_rect.xw);
		float3 n3 = DecodeOctahedralMap(sample_rect.zw);
		
		float solid_angle = TriangleSolidAngle(n0, n1, n3) + TriangleSolidAngle(n0, n3, n2);
		
		// Scale tile_cdf_solid_angle by cdf_tile_area * (1.0 / PI) to get a reasonable value range to store in a texture.
		tile_cdf_solid_angle[thread_id] = solid_angle * LightingConstants::cdf_tile_area * (1.0 / PI);
		// tile_cdf_solid_angle[thread_id] = 4.0;
	}
}
#endif // defined(ENERGY_COMPENSATION_LUT)
