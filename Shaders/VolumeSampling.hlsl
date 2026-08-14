#ifndef VOLUMESAMPLING_HLSL
#define VOLUMESAMPLING_HLSL
#include "Basic.hlsl"

// Ratio tracking estimator is based on https://pbr-book.org/4ed/Volume_Scattering/Transmittance#
float TraceVolumetricMediumTransmittanceRay(float3 origin, float3 direction, float t_max, inout uint hash, float noise_mip_level) {
	VolumetricSceneIntersection intersection = RayBoxIntersection(origin - scene.clouds.world_space_position, direction, scene.clouds.world_space_size);
	if (intersection.is_hit == false) return 1.0;
	
	float ray_t = intersection.t_min;
	intersection.t_max = min(intersection.t_max, t_max);
	
	if (ray_t >= intersection.t_max) return 1.0;
	
	float transmittance = 1.0;
	
	VoxelTraversalState traversal_state = BeginVoxelTraversal(origin, direction, ray_t, intersection.t_max);
	
	uint max_iterations = 1024;
	for (uint i = 0; i < max_iterations; i += 1) {
		float2 u = ComputeRandomUnorm16x2(hash);
		
		ray_t = VoxelGridSkipEmptySpace(traversal_state, direction, ray_t);
		ray_t += SampleExponentialDistribution(u.x, scene.clouds.extinction_coefficients);
		
		if (ray_t < intersection.t_max) {
			float3 position = direction * ray_t + origin;
			float density = ComputeVolumetricMediumDensity(position, noise_mip_level);
			
			transmittance *= (1.0 - density);
		} else {
			i = max_iterations;
		}
		
		if (RussianRoulette(transmittance, 0.1, u.y)) {
			i = max_iterations;
		}
	}
	
	return transmittance;
}

VolumeInteractionType SampleVolumetricMedium(inout RayDesc ray_desc, float t_max, inout uint hash, float noise_mip_level) {
	VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - scene.clouds.world_space_position, ray_desc.Direction, scene.clouds.world_space_size);
	if (intersection.is_hit == false) return VolumeInteractionType::None;
	
	float ray_t = intersection.t_min;
	intersection.t_max = min(intersection.t_max, t_max);
	
	if (ray_t >= intersection.t_max) return VolumeInteractionType::None;
	
	VolumeInteractionType interaction_type = VolumeInteractionType::None;
	
	VoxelTraversalState traversal_state = BeginVoxelTraversal(ray_desc.Origin, ray_desc.Direction, intersection.t_min, intersection.t_max);
	
	uint max_iterations = 1024;
	for (uint i = 0; i < max_iterations; i += 1) {
		float2 u = ComputeRandomUnorm16x2(hash);
		
		ray_t = VoxelGridSkipEmptySpace(traversal_state, ray_desc.Direction, ray_t);
		ray_t += SampleExponentialDistribution(u.x, scene.clouds.extinction_coefficients);
		
		if (ray_t < intersection.t_max) {
			float3 position = ray_desc.Direction * ray_t + ray_desc.Origin;
			float density = ComputeVolumetricMediumDensity(position, noise_mip_level);
			
			float p_absorption = (scene.clouds.absorption_coefficients / scene.clouds.extinction_coefficients) * density;
			float p_scattering = (scene.clouds.scattering_coefficients / scene.clouds.extinction_coefficients) * density;
			
			if (u.y < p_absorption) {
				i = max_iterations;
				interaction_type = VolumeInteractionType::Absorption;
			} else if (u.y < (p_absorption + p_scattering)) {
				i = max_iterations;
				interaction_type = VolumeInteractionType::Scattering;
				
				ray_desc.Origin = position;
			}
		} else {
			i = max_iterations;
		}
	}
	
	return interaction_type;
}

#endif // VOLUMESAMPLING_HLSL
