#ifndef CLOUDSAMPLING_HLSL
#define CLOUDSAMPLING_HLSL
#include "Basic.hlsl"

struct VolumetricSceneIntersection {
	float t_min;
	float t_max;
	bool is_hit;
};

// Box is assumed to be in [0, extent] range. Intersection ray time is limited between [0, ray_t_max=inf].
VolumetricSceneIntersection RayBoxIntersection(float3 ray_origin, float3 ray_direction, float3 extent, float ray_t_max = asfloat(0x7F800000)) {
	float3 inv_direction = select(ray_direction == 0.0, /*nan*/asfloat(0x7FC00000), 1.0 / ray_direction); // See @inv_direction for reference.
	
	float3 t_min = -ray_origin * inv_direction;
	float3 t_max = extent * inv_direction + t_min;
	
	float3 t0 = min(t_min, t_max);
	float3 t1 = max(t_min, t_max);
	
	VolumetricSceneIntersection intersection;
	intersection.t_min = max(max(t0.x, t0.y), max(t0.z, 0.0));
	intersection.t_max = min(min(t1.x, t1.y), min(t1.z, ray_t_max));
	intersection.is_hit = (intersection.t_min < intersection.t_max);
	
	return intersection;
}

// Based on "Methods (and madness) to model and render immersive real-time voxel-based clouds." by Andrew Schneider.
float ComputeCloudMediumDensity(float3 position, float noise_mip_level) {
	float3 sample_uvw = (position - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size;
	float dimensional_profile = sdf_cloud_volume.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
	
	if (dimensional_profile < (0.5 / 255.0)) return 0.0;
	
	Texture3D<float4> density_noise = ResourceDescriptorHeap[scene.clouds.density_noise_index];
	float4 noise = density_noise.SampleLevel(sampler_linear_wrap, position * scene.clouds.density_noise_scale + float3(scene.clouds.density_noise_offset, 0.0), 0);
	
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


// Based on the ideas from https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/
struct VoxelTraversalState {
	float3 inv_direction;
	float3 origin;
	float ray_t_max;
};

VoxelTraversalState BeginVoxelTraversal(float3 origin, float3 direction, float ray_t_max) {
	VoxelTraversalState state;
	state.inv_direction = select(direction == 0.0, /*nan*/asfloat(0x7FC00000), 1.0 / direction); // See @inv_direction for reference.
	state.origin        = (origin - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size.x + 1.0;
	state.ray_t_max     = ray_t_max * scene.clouds.inv_world_space_size.x;
	
	return state;
}

float VoxelGridSkipEmptySpace(VoxelTraversalState state, float3 direction, float ray_t_min) {
	float ray_t = ray_t_min * scene.clouds.inv_world_space_size.x;
	float3 position = clamp(state.origin + direction * ray_t, 1.0, asfloat(0x3FFFFFFF)); // [1, 2)
	
	uint max_iterations = CloudConstants::mask_volume_size_bits.x + CloudConstants::mask_volume_size_bits.y + CloudConstants::mask_volume_size_bits.z - 1;
	for (uint i = 0; i < max_iterations && ray_t < state.ray_t_max; i += 1) {
		uint3 position_u32 = asuint(position);
		
		uint3 volume_coordinates = (position_u32 >> 18) & 0x1F;
		uint  voxel_index = RowMajorEncode3D(position_u32 >> 16, 2);
		
		u64 voxel = sdf_cloud_volume_mask[volume_coordinates];
		
		if (((voxel >> voxel_index) & 0x1) == 0) {
			bool skip_voxel  = (voxel == 0);
			bool skip_octant = ((voxel >> (voxel_index & 0b101010)) & 0x00330033) == 0;
			
			uint scale_exp = skip_voxel ? 18 : (skip_octant ? 17 : 16);
			float scale = asfloat((scale_exp - 23u + 127u) << 23u);
			
			float3 voxel_min = asfloat(asuint(position) & (u32_max << scale_exp));
			float3 voxel_far = voxel_min + select(direction >= 0.0, scale, 0.0);
			
			// @inv_direction might contain NANs, they would get rejected by min, which always returns non NAN argument.
			float3 far_intersection = (voxel_far - state.origin) * state.inv_direction;
			ray_t = min(min(far_intersection.x, far_intersection.y), far_intersection.z);
			
			float3 next_voxel_min = select(ray_t == far_intersection, voxel_min + select(direction >= 0.0, +scale, -scale), voxel_min);
			float3 next_voxel_max = asfloat(asint(next_voxel_min) + ((1u << scale_exp) - 1));
			
			position = clamp(state.origin + direction * ray_t, next_voxel_min, next_voxel_max);
		} else {
			i = max_iterations;
		}
	}
	
	return ray_t * scene.clouds.world_space_size.x;
}

#endif // CLOUDSAMPLING_HLSL
