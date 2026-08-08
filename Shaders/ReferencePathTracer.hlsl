#include "Basic.hlsl"

#if defined(REFERENCE_PATH_TRACER)
#include "LightEvaluation.hlsl"
#include "GeometrySampling.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

compile_const u32 thread_group_size = 32;

#define ENABLE_VOLUME_RENDERING 0

#if ENABLE_VOLUME_RENDERING
struct VolumetricSceneIntersection {
	float t_min;
	float t_max;
	bool is_hit;
};

// TODO: Skip empty space inside the box.
// Based on https://www.shadertoy.com/view/ld23DV see license in THIRD_PARTY_LICENSES.md
VolumetricSceneIntersection RayBoxIntersection(float3 ray_origin, float3 ray_direction, float3 half_extent) {
	ray_origin -= half_extent;
	
	float3 m = 1.0 / ray_direction;
	float3 n = m * ray_origin;
	
	float3 k = abs(m) * half_extent;
	
	float3 t0 = -n - k;
	float3 t1 = -n + k;
	
	VolumetricSceneIntersection intersection;
	intersection.t_min = max(max(t0.x, t0.y), t0.z);
	intersection.t_max = min(min(t1.x, t1.y), t1.z);
	intersection.is_hit = (intersection.t_min <= intersection.t_max) && (intersection.t_max > 0.0);
	
	return intersection;
}


// TODO: Create an entity to store this information.
compile_const float  grid_to_world_scale = (1.0 / 4.0);
compile_const float  grid_voxel_size = 4.0;
compile_const float3 box_size     = float3(82.0, 117.0, 41.0) * grid_voxel_size * grid_to_world_scale;
compile_const float3 box_position = float3(-10.0, -15.0, 25.0);
compile_const float sdf_band_size_meters = 16.0;
compile_const float material_scattering_coefficients = 1.0 / grid_to_world_scale;
compile_const float material_absorption_coefficients = 0.0;
compile_const float material_extinction_coefficients = material_absorption_coefficients + material_scattering_coefficients;
compile_const float material_g = 0.4;
compile_const Texture3D<float>  sdf_grid      = ResourceDescriptorHeap[26];
compile_const Texture3D<float4> density_noise = ResourceDescriptorHeap[27];


// Based on "Methods (and madness) to model and render immersive real-time voxel-based clouds." by Andrew Schneider.
float ComputeVolumetricMediumDensity(float3 position) {
	float3 sample_uvw = (position - box_position) / box_size;
	float dimensional_profile = sdf_grid.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
	
	float4 noise = density_noise.SampleLevel(sampler_linear_wrap, position * (1.0 / 64.0) / grid_to_world_scale, 0);
	
	float wispy_noise_scale   = 3.33;
	float billowy_noise_scale = 0.8;
	float cloud_type          = smoothstep(0.2, 0.5, sample_uvw.z);
	float density_scale       = 1.0;
	
	float wispy_noise = saturate(lerp(noise.x, noise.y, dimensional_profile) * wispy_noise_scale);
	float billowy_type_gradient = pow(dimensional_profile, 0.25);
	float billowy_noise = saturate(lerp(noise.z, noise.w, billowy_type_gradient) * billowy_noise_scale);
	float noise_composite = lerp(wispy_noise, billowy_noise, cloud_type);
	
	float uprezzed_density = saturate((dimensional_profile - noise_composite) / (1.0 - noise_composite)) * density_scale;
	uprezzed_density = pow(uprezzed_density, lerp(0.3, 0.6, max(pow(density_scale, 4.0), 1.0 / 1024.0)));
	
	return uprezzed_density;
}


// Ratio tracking estimator is based on https://pbr-book.org/4ed/Volume_Scattering/Transmittance#
float TraceVolumetricMediumTransmittanceRay(float3 origin, float3 direction, inout uint hash) {
	VolumetricSceneIntersection intersection = RayBoxIntersection(origin - box_position, direction, box_size * 0.5);
	if (intersection.is_hit == false) return 1.0;
	
	float ray_t = max(intersection.t_min, 0.0);
	float transmittance = 1.0;
	
	uint max_iterations = 1024;
	for (uint i = 0; i < max_iterations; i += 1) {
		float2 u = ComputeRandomUnorm16x2(hash);
		
		ray_t += SampleExponentialDistribution(u.x, material_extinction_coefficients);
		if (ray_t > intersection.t_max) break;
		
		float3 position = direction * ray_t + origin;
		float density = ComputeVolumetricMediumDensity(position);
		
		transmittance *= (1.0 - density);
		
		if (RussianRoulette(transmittance, 0.1, u.y)) {
			i = max_iterations;
		}
	}
	
	return transmittance;
}

VolumeInteractionType SampleVolumetricMedium(inout LightAccumulator light_accumulator, inout RayDesc ray_desc, float t_max, inout uint hash) {
	VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - box_position, ray_desc.Direction, box_size * 0.5);
	intersection.t_min = max(intersection.t_min, 0.0);
	intersection.t_max = min(intersection.t_max, t_max);
	
	if (intersection.is_hit == false) return VolumeInteractionType::None;
	
	VolumeInteractionType interaction_type = VolumeInteractionType::None;
	
	uint max_iterations = 1024;
	for (uint i = 0; i < max_iterations; i += 1) {
		float2 u = ComputeRandomUnorm16x2(hash);
		
		float ray_t = intersection.t_min + SampleExponentialDistribution(u.x, material_extinction_coefficients);
		
		if (ray_t < intersection.t_max) {
			float3 position = ray_desc.Direction * ray_t + ray_desc.Origin;
			float density = ComputeVolumetricMediumDensity(position);
			
			float p_absorption = (material_absorption_coefficients / material_extinction_coefficients) * density;
			float p_scattering = (material_scattering_coefficients / material_extinction_coefficients) * density;
			
			if (u.y < p_absorption) {
				i = max_iterations;
				interaction_type = VolumeInteractionType::Absorption;
			} else if (u.y < (p_absorption + p_scattering)) {
				i = max_iterations;
				interaction_type = VolumeInteractionType::Scattering;
				
				ray_desc.Origin = position;
			} else {
				intersection.t_min = ray_t;
			}
		} else {
			i = max_iterations;
		}
	}
	
	return interaction_type;
}
#endif // ENABLE_VOLUME_RENDERING


struct PathTracerShadowSampler {
	uint hash;
	
	float EvaluateVisibility(float3 ray_origin, float3 ray_direction, float ray_length) {
		RayQuery<
			RAY_FLAG_CULL_NON_OPAQUE |
			RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
			RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
			RAY_FLAG_NONE
		> ray_query;
		
		float2 penumbra_noise = ConcentricMapping(ComputeRandomUnorm16x2(hash));
		RayDesc ray_desc = ShadowSampler::CreateShadowRay(ray_origin, ray_direction, ray_length, penumbra_noise);
		
		ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
		
		while (ray_query.Proceed()) {
			
		}
		
		float transmittance = ray_query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
		
#if ENABLE_VOLUME_RENDERING
		if (transmittance == 1.0) {
			transmittance = TraceVolumetricMediumTransmittanceRay(ray_desc.Origin, ray_desc.Direction, hash);
		}
#endif // ENABLE_VOLUME_RENDERING
		
		return transmittance;
	}
};


[ThreadGroupSize(thread_group_size, 1, 1)][WaveSize(thread_group_size)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = constants.mode == ReferencePathTracerMode::Accumulation ? group_id * uint2(8, 4) + MortonDecode(thread_index) : group_id;
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), constants.mode == ReferencePathTracerMode::Accumulation ? scene.path_tracer_accumulated_frame_count : thread_index);
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
		
#if ENABLE_VOLUME_RENDERING
		VolumeInteractionType volume_interaction_type = SampleVolumetricMedium(light_accumulator, ray_desc, ray_query.CommittedRayT(), hash);
#else // !ENABLE_VOLUME_RENDERING
		compile_const VolumeInteractionType volume_interaction_type = VolumeInteractionType::None;
#endif // !ENABLE_VOLUME_RENDERING
		
		if (volume_interaction_type == VolumeInteractionType::Absorption) {
			i = max_path_length;
		} else if (volume_interaction_type == VolumeInteractionType::Scattering) {
#if ENABLE_VOLUME_RENDERING
			LightSample light_sample = SampleLightUniform(ray_desc.Origin, hash);
			
			float3 wo = -ray_desc.Direction;
			
			if (light_sample.light_entity_index != u32_max) {
				PathTracerShadowSampler shadow_sampler;
				shadow_sampler.hash = WyHash32(hash, 0xB97FB0B9);
				
				EvaluatePhaseFunction(
					light_accumulator,
					shadow_sampler,
					ray_desc.Origin,
					wo,
					material_g,
					throughput,
					light_sample
				);
			}
			
			ray_desc.Direction = SamplePhaseFunctionHG(wo, material_g, ComputeRandomUnorm16x2(hash));
#endif // ENABLE_VOLUME_RENDERING
		} else if (ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
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
			
			float3 single_scattering_energy = SamplePreintegratedBrdfTable(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
			
			if (light_sample.light_entity_index != u32_max) {
				PathTracerShadowSampler shadow_sampler;
				shadow_sampler.hash = WyHash32(hash, 0xD0AF39A7);
				
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
			float3 sky_radiance = SampleSkyPanoramaLUT(scene.atmosphere, sky_panorama_lut, transmittance_lut, scene.world_space_camera_position, ray_desc.Direction, i == 0);
			
			light_accumulator.radiance += throughput * sky_radiance;
			i = max_path_length;
		}
	}
	
	
	uint reference_path_tracer_min_x = (uint)(scene.render_target_size.x * scene.reference_path_tracer_percent);
	if (constants.mode == ReferencePathTracerMode::Accumulation) {
		float3 old_accumulated_radiance = max(path_tracer_radiance[thread_id].xyz, 0.0);
		float3 new_accumulated_radiance = (old_accumulated_radiance * (scene.path_tracer_accumulated_frame_count - 1) + light_accumulator.radiance) / (float)scene.path_tracer_accumulated_frame_count;
		path_tracer_radiance[thread_id] = float4(new_accumulated_radiance, 1.0);
		
		if (thread_id.x < reference_path_tracer_min_x) {
			scene_radiance[thread_id] = float4(new_accumulated_radiance * scene.exposure_estimate, 1.0);
		}
	} else {
		float3 new_accumulated_radiance = WaveActiveSum(light_accumulator.radiance) / (float)WaveGetLaneCount();
		
		if (thread_id.x < reference_path_tracer_min_x) {
			scene_radiance[thread_id] = float4(new_accumulated_radiance * scene.exposure_estimate, 1.0);
		}
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

groupshared float3 gs_single_scattering_energy[thread_group_area / min_wave_size];
groupshared float2 gs_preintegrated_brdf[thread_group_area / min_wave_size];

[ThreadGroupSize(thread_group_area, 1, 1)][WaveSize(min_wave_size, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	float2 group_uv = group_id * (1.0 / (energy_compensation_lut_size - 1));
	
	float roughness    = group_uv.y;
	float alpha        = Pow2(roughness);
	float alpha_square = Pow2(alpha);
	float cos_theta    = saturate(group_uv.x + energy_compensation_lut_cos_theta_bias);
	
	float3 wo = float3(sqrt(1.0 - Pow2(cos_theta)), 0.0, cos_theta);
	
	float3 single_scattering_energy = 0.0;
	float2 preintegrated_brdf = 0.0;
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
			
			single_scattering_energy.z += (1.0 - fresnel); // Indirect diffuse
			
			
			float fresnel_zero_reflectance = FresnelSchlick(0.0, dot(wo, wh));
			preintegrated_brdf.x += specular_sample * (1.0 - fresnel_zero_reflectance);
			preintegrated_brdf.y += specular_sample * fresnel_zero_reflectance;
		}
	}
	
	float3 wave_single_scattering_energy = WaveActiveSum(single_scattering_energy);
	float2 wave_preintegrated_brdf = WaveActiveSum(preintegrated_brdf);
	if (WaveIsFirstLane()) {
		gs_single_scattering_energy[thread_index / WaveGetLaneCount()] = wave_single_scattering_energy;
		gs_preintegrated_brdf[thread_index / WaveGetLaneCount()] = wave_preintegrated_brdf;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		float3 group_single_scattering_energy = wave_single_scattering_energy;
		float2 group_preintegrated_brdf = wave_preintegrated_brdf;
		
		u32 wave_count = thread_group_area / WaveGetLaneCount();
		for (u32 i = 1; i < wave_count; i += 1) {
			group_single_scattering_energy += gs_single_scattering_energy[i];
			group_preintegrated_brdf += gs_preintegrated_brdf[i];
		}
		
		ggx_single_scattering_energy_lut[group_id] = float4(group_single_scattering_energy / sample_count, 0.0);
		ggx_preintegrated_brdf_lut[group_id]       = group_preintegrated_brdf / sample_count;
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
