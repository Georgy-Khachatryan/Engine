#ifndef VOLUMESAMPLING_HLSL
#define VOLUMESAMPLING_HLSL
#include "Basic.hlsl"

float ComputeFogMediumDensity(float z) {
	return exp(-scene.fog.height_falloff * (z - scene.fog.world_space_position.z));
}

float ComputeFogMediumTransmittance(float3 origin, float3 direction, float t_min, float t_max) {
	float transmittance = 1.0;
	
	if (direction.z == 0.0) {
		float delta_t = (t_max - t_min);
		float distance = exp(-scene.fog.height_falloff * (origin.z - scene.fog.world_space_position.z)) * delta_t;
		transmittance *= exp(-scene.fog.extinction_coefficients * distance);
	} else {
		float distance = (exp(-scene.fog.height_falloff * (origin.z + direction.z * t_min - scene.fog.world_space_position.z)) - exp(-scene.fog.height_falloff * (origin.z + direction.z * t_max - scene.fog.world_space_position.z))) / (scene.fog.height_falloff * direction.z);
		transmittance *= exp(-scene.fog.extinction_coefficients * distance);
	}
	
	return transmittance;
}

float SampleFogMediumDistance(float3 origin, float3 direction, float t_min, float t_max, float u) {
	float t = 0.0;
	
	if (direction.z == 0.0) {
		t = SampleExponentialDistribution(u, scene.fog.extinction_coefficients * ComputeFogMediumDensity(origin.z));
	} else {
		float distance = SampleExponentialDistributionRcp(u, scene.fog.inv_extinction_coefficients);
		float z_min = origin.z + direction.z * t_min - scene.fog.world_space_position.z;
		t = log(exp(-scene.fog.height_falloff * z_min) - scene.fog.height_falloff * distance * direction.z) / (-scene.fog.height_falloff * direction.z) - (z_min / direction.z);
	}
	
	return t;
}

float TraceVolumetricMediumTransmittanceRay(float3 origin, float3 direction, float t_max, inout uint hash, float noise_mip_level) {
	float transmittance = 1.0;
	
	// Sample fog transmittance analytically.
	if (scene.feature_flags & SceneFeatureFlags::Fog) {
		VolumetricSceneIntersection intersection = RayBoxIntersection(origin - scene.fog.world_space_position, direction, scene.fog.world_space_size, t_max);
		
		if (intersection.is_hit) {
			transmittance = ComputeFogMediumTransmittance(origin, direction, intersection.t_min, intersection.t_max);
		}
	}
	
	// Sample cloud transmittance using ratio tracking estimator, which is based on https://pbr-book.org/4ed/Volume_Scattering/Transmittance#
	if (scene.feature_flags & SceneFeatureFlags::Clouds) {
		VolumetricSceneIntersection intersection = RayBoxIntersection(origin - scene.clouds.world_space_position, direction, scene.clouds.world_space_size, t_max);
		
		if (intersection.is_hit) {
			float ray_t = intersection.t_min;
			
			VoxelTraversalState traversal_state = BeginVoxelTraversal(origin, direction, intersection.t_max);
			
			uint max_iterations = 1024;
			for (uint i = 0; i < max_iterations; i += 1) {
				float2 u = ComputeRandomUnorm16x2(hash);
				
				ray_t = VoxelGridSkipEmptySpace(traversal_state, direction, ray_t);
				ray_t += SampleExponentialDistributionRcp(u.x, scene.clouds.inv_extinction_coefficients);
				
				if (ray_t < intersection.t_max) {
					float3 position = direction * ray_t + origin;
					float density = ComputeCloudMediumDensity(position, noise_mip_level);
					
					transmittance *= (1.0 - density);
				} else {
					i = max_iterations;
				}
				
				if (RussianRoulette(transmittance, 0.1, u.y)) {
					i = max_iterations;
				}
			}
		}
	}
	
	return transmittance;
}

VolumeInteractionType SampleVolumetricMedium(inout RayDesc ray_desc, float t_max, inout uint hash, float noise_mip_level) {
	float sample_t = t_max;
	VolumeInteractionType interaction_type = VolumeInteractionType::None;
	
	// Sample fog free path distance using closed form tracking.
	if (scene.feature_flags & SceneFeatureFlags::Fog) {
		VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - scene.fog.world_space_position, ray_desc.Direction, scene.fog.world_space_size, sample_t);
		
		if (intersection.is_hit) {
			float2 u = ComputeRandomUnorm16x2(hash);
			
			float ray_t = intersection.t_min + SampleFogMediumDistance(ray_desc.Origin, ray_desc.Direction, intersection.t_min, intersection.t_max, u.x);
			
			if (ray_t < intersection.t_max) {
				float p_absorption = scene.fog.absorption_probability;
				float p_scattering = scene.fog.scattering_probability;
				
				if (u.y < p_absorption) {
					interaction_type = VolumeInteractionType::Absorption;
					sample_t = ray_t;
				} else /*if (u.y < (p_absorption + p_scattering))*/ {
					interaction_type = VolumeInteractionType::Scattering;
					sample_t = ray_t;
				}
			}
		}
	}
	
	// Sample cloud free path distance using delta tracking.
	if (scene.feature_flags & SceneFeatureFlags::Clouds) {
		VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - scene.clouds.world_space_position, ray_desc.Direction, scene.clouds.world_space_size, sample_t);
		
		if (intersection.is_hit) {
			float ray_t = intersection.t_min;
			
			VoxelTraversalState traversal_state = BeginVoxelTraversal(ray_desc.Origin, ray_desc.Direction, intersection.t_max);
			
			uint max_iterations = 1024;
			for (uint i = 0; i < max_iterations; i += 1) {
				float2 u = ComputeRandomUnorm16x2(hash);
				
				ray_t = VoxelGridSkipEmptySpace(traversal_state, ray_desc.Direction, ray_t);
				ray_t += SampleExponentialDistributionRcp(u.x, scene.clouds.inv_extinction_coefficients);
				
				if (ray_t < intersection.t_max) {
					float3 position = ray_desc.Direction * ray_t + ray_desc.Origin;
					float density = ComputeCloudMediumDensity(position, noise_mip_level);
					
					float p_absorption = scene.clouds.absorption_probability * density;
					float p_scattering = scene.clouds.scattering_probability * density;
					
					if (u.y < p_absorption) {
						i = max_iterations;
						
						interaction_type = VolumeInteractionType::Absorption;
						sample_t = ray_t;
					} else if (u.y < (p_absorption + p_scattering)) {
						i = max_iterations;
						
						interaction_type = VolumeInteractionType::Scattering;
						sample_t = ray_t;
					}
				} else {
					i = max_iterations;
				}
			}
		}
	}
	
	if (interaction_type == VolumeInteractionType::Scattering) {
		ray_desc.Origin = ray_desc.Direction * sample_t + ray_desc.Origin;
	}
	
	return interaction_type;
}

#endif // VOLUMESAMPLING_HLSL
