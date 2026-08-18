#include "Basic.hlsl"
#if !defined(SHADOW_MAP_FILTER)
#include "AtmosphereSampling.hlsl"
#include "BrdfSampling.hlsl"
#include "CloudSampling.hlsl"
#endif // !defined(SHADOW_MAP_FILTER)

compile_const float hybrid_transmittance_ray_length = 8.0;

#if defined(CLOUD_RAYMARCH) || defined(RADIANCE_TRANSFER_VOLUME)
float RaymarchHybridOpticalDepthRay(float3 origin, float3 direction, float noise_mip_level) {
	float optical_depth = 0.0;
	
	float sample_count = 3.0;
	
	float prev_ray_t = 0.0;
	for (float t = (1.0 / sample_count); t < 1.0; t += (1.0 / sample_count)) {
		float ray_t = Pow2(t) * hybrid_transmittance_ray_length;
		
		float3 position = direction * ray_t + origin;
		float density = ComputeVolumetricMediumDensity(position, noise_mip_level);
		
		float extinction_coefficients = density;
		optical_depth += extinction_coefficients * (ray_t - prev_ray_t);
		prev_ray_t = ray_t;
	}
	
	float3 sample_uvw = (origin - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size;
	float cloud_optical_depth = cloud_optical_depth_volume.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
	
	return optical_depth * scene.clouds.extinction_coefficients + cloud_optical_depth;
}
#endif // defined(CLOUD_RAYMARCH) || defined(RADIANCE_TRANSFER_VOLUME)

#if defined(CLOUD_RAYMARCH) || defined(OPTICAL_DEPTH_VOLUME) || defined(SHADOW_MAP)
void SkipEmptySpaceInTwoDirections(RayDesc ray_desc, inout VolumetricSceneIntersection intersection) {
	VoxelTraversalState forward_traversal_state = BeginVoxelTraversal(ray_desc.Origin, ray_desc.Direction, intersection.t_max);
	intersection.t_min = VoxelGridSkipEmptySpace(forward_traversal_state, ray_desc.Direction, intersection.t_min);
	
	VoxelTraversalState backward_traversal_state = BeginVoxelTraversal(ray_desc.Origin + ray_desc.Direction * intersection.t_max, -ray_desc.Direction, intersection.t_max - intersection.t_min);
	intersection.t_max -= VoxelGridSkipEmptySpace(backward_traversal_state, -ray_desc.Direction, 0.0);
}
#endif // defined(CLOUD_RAYMARCH) || defined(OPTICAL_DEPTH_VOLUME) || defined(SHADOW_MAP)

#if defined(CLOUD_RAYMARCH)
[ThreadGroupSize(256, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * 16 + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	
	VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - scene.clouds.world_space_position, ray_desc.Direction, scene.clouds.world_space_size);
	if (intersection.is_hit == false) return;
	
	float depth = depth_stencil[thread_id];
	if (depth != 0.0) {
		float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
		intersection.t_max = min(intersection.t_max, length(view_space_position));
	}
	
	SkipEmptySpaceInTwoDirections(ray_desc, intersection);
	
	float3 scattering = 0.0;
	float transmittance = 1.0;
	float cloud_t_min = intersection.t_max;
	
	float sample_count = 0.0;
	float delta_t = 8.0;
	float ray_t = intersection.t_min + delta_t * LoadBlueNoise(blue_noise_1d, thread_id, scene.frame_index);
	for (; (ray_t < intersection.t_max) && (transmittance > (1.0 / 256.0)); ray_t += delta_t, sample_count += 1.0) {
		float3 position = ray_desc.Direction * ray_t + ray_desc.Origin;
		float noise_mip_level = (float)min(FloorLog2(max(ray_t * scene.clouds.raymarch_noise_mip_scale, 1.0)), 7);
		
		float density = ComputeVolumetricMediumDensity(position, noise_mip_level);
		float extinction_coefficients = density * scene.clouds.extinction_coefficients;
		float scattering_coefficients = density * scene.clouds.scattering_coefficients;
		
		if (extinction_coefficients > (1.0 / 1024.0)) {
			float sun_optical_depth = RaymarchHybridOpticalDepthRay(position, scene.atmosphere.world_space_sun_direction, noise_mip_level);
			
			float3 sample_uvw = (position - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size;
			float2 radiance_transfer = cloud_radiance_transfer_volume.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
			
			// scattering_coefficients / extinction_coefficients could be precomputed. For clouds it's generally 1.0
			float slice_transmittance = exp(-extinction_coefficients * delta_t);
			float scattering_integral = transmittance * scattering_coefficients * (1.0 - slice_transmittance) / extinction_coefficients;
			
			scattering.x += exp(-sun_optical_depth) * scattering_integral;
			scattering.y += radiance_transfer.x     * scattering_integral;
			scattering.z += radiance_transfer.y     * scattering_integral;
			transmittance *= slice_transmittance;
			
			cloud_t_min = min(cloud_t_min, ray_t);
		}
	}
	
	float phase_function_g = scene.clouds.scattering_anisotropy;
	scattering.x *= PhaseFunctionHG(dot(-ray_desc.Direction, scene.atmosphere.world_space_sun_direction), phase_function_g);
	
	float3 sky_irradiance = average_sky_irradiance[uint2(0, 0)];
	float3 sun_irradiance = scene.atmosphere.sun_color * scene.atmosphere.sun_irradiance * SampleTransmittanceLUT(scene.atmosphere, transmittance_lut, ray_desc.Direction * cloud_t_min + ray_desc.Origin);
	
	float3 radiance = sun_irradiance * scattering.x + sun_irradiance * scattering.y + scattering.z * sky_irradiance;
	scene_radiance[thread_id] = float4(scene_radiance[thread_id].xyz * transmittance + radiance * scene.exposure_estimate, 1.0);
}
#endif // defined(CLOUD_RAYMARCH)


#if defined(OPTICAL_DEPTH_VOLUME)
float RaymarchOpticalDepthRay(float3 origin, float3 direction, float t_max) {
	float optical_depth = 0.0;
	float noise_mip_level = scene.clouds.lighting_volume_noise_mip_offset;
	
	float delta_t = 4.0;
	for (float ray_t = hybrid_transmittance_ray_length; ray_t < t_max; ray_t += delta_t, noise_mip_level += 1.0) {
		float3 position = direction * ray_t + origin;
		float density = ComputeVolumetricMediumDensity(position, min(noise_mip_level, 7.0));
		
		float extinction_coefficients = density;
		optical_depth += extinction_coefficients * delta_t;
	}
	
	return optical_depth * scene.clouds.extinction_coefficients;
}

[ThreadGroupSize(64, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint update_command = cloud_update_list[group_id];
	uint3 thread_id = RowMajorDecode3D(update_command, 8) * 4 + RowMajorDecode3D(thread_index, 2);
	
	float3 thread_uv = (thread_id + 0.5) / CloudConstants::lighting_volume_size;
	float3 world_space_position = thread_uv * scene.clouds.world_space_size + scene.clouds.world_space_position;
	
	RayDesc ray_desc;
	ray_desc.Origin    = world_space_position;
	ray_desc.Direction = scene.atmosphere.world_space_sun_direction;
	
	VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - scene.clouds.world_space_position, ray_desc.Direction, scene.clouds.world_space_size);
	SkipEmptySpaceInTwoDirections(ray_desc, intersection);
	
	float optical_depth = RaymarchOpticalDepthRay(ray_desc.Origin + ray_desc.Direction * intersection.t_min, ray_desc.Direction, intersection.t_max - intersection.t_min);
	cloud_optical_depth_volume[thread_id] = optical_depth;
}
#endif // defined(OPTICAL_DEPTH_VOLUME)


#if defined(SHADOW_MAP)
float3 RaymarchBeerShadowMapRay(float3 origin, float3 direction, float t_min, float t_max) {
	float noise_mip_level = scene.clouds.lighting_volume_noise_mip_offset;
	
	float optical_depth = 0.0;
	float cloud_t_max   = t_min;
	float cloud_t_min   = t_max;
	
	float delta_t = 4.0;
	for (float ray_t = t_min; ray_t < t_max; ray_t += delta_t, noise_mip_level += 1.0) {
		float3 position = direction * ray_t + origin;
		float density = ComputeVolumetricMediumDensity(position, min(noise_mip_level, 7.0));
		
		if (density > (1.0 / 255.0)) {
			cloud_t_max = max(cloud_t_max, ray_t);
			cloud_t_min = min(cloud_t_min, ray_t);
			optical_depth += density * delta_t;
		}
	}
	
	bool is_valid = (optical_depth != 0.0);
	return is_valid ? float3(cloud_t_min, cloud_t_max, optical_depth * scene.clouds.extinction_coefficients) : float3(t_min, t_max, 0.0);
}

[ThreadGroupSize(256, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * 16 + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) / CloudConstants::shadow_map_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clouds.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.clouds.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.clouds.view_to_world, view_space_ray.direction);
	
	VolumetricSceneIntersection intersection = RayBoxIntersection(ray_desc.Origin - scene.clouds.world_space_position, ray_desc.Direction, scene.clouds.world_space_size);
	SkipEmptySpaceInTwoDirections(ray_desc, intersection);
	
	float3 shadow_map = RaymarchBeerShadowMapRay(ray_desc.Origin, ray_desc.Direction, intersection.t_min, intersection.t_max);
	transient_cloud_shadow_map[thread_id] = shadow_map;
}
#endif // defined(SHADOW_MAP)


#if defined(SHADOW_MAP_FILTER)
#include "TextureSampling.hlsl"

[ThreadGroupSize(256, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * 16 + MortonDecode(thread_index);
	
	float3 shadow_map_sum = 0.0;
	float  weight_sum = 0.0;
	
	compile_const s32 radius = 1;
	for (s32 y = -radius; y <= radius; y += 1) {
		for (s32 x = -radius; x <= radius; x += 1) {
			float weight = ComputeGaussianWeight(x, y, radius);
			s32x2 sample_position = thread_id + s32x2(x, y);
			
			if (all(sample_position >= 0) && all(sample_position < CloudConstants::shadow_map_size)) {
				shadow_map_sum += transient_cloud_shadow_map[sample_position] * weight;
				weight_sum += weight;
			}
		}
	}
	
	cloud_shadow_map[thread_id] = shadow_map_sum * rcp(weight_sum);
}
#endif // defined(SHADOW_MAP_FILTER)


#if defined(RADIANCE_TRANSFER_VOLUME)
#include "VolumeSampling.hlsl"

#define USE_OPTICAL_DEPTH_VOLUME

groupshared float2 gs_radiance_transfer[8];

[ThreadGroupSize(64, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint update_command = cloud_update_list[group_id];
	uint3 thread_id = RowMajorDecode3D(update_command, 8) * 4 + RowMajorDecode3D(thread_index, 2);
	
	uint hash = WyHash32(RowMajorEncode3D(thread_id, 10), scene.frame_index);
	
	float3 thread_uv = (thread_id + ComputeRandomUnorm10x3(hash)) / CloudConstants::lighting_volume_size;
	float3 world_space_position = thread_uv * scene.clouds.world_space_size + scene.clouds.world_space_position;
	
	// uint max_path_length = 32;
	uint max_path_length = 4;
	
	// Use isotropic phase function on the first scattering vertex to make sure radiance transfer is view independent.
	// This also allows us to sample the radiance transfer volume for the last vertex on the path to get more bounces.
	RayDesc ray_desc;
	ray_desc.Origin    = world_space_position;
	ray_desc.Direction = SphereMapping(ComputeRandomUnorm16x2(hash));
	
	u32 history_frame_count = cloud_sample_count_volume[thread_id / 4];
	
	if (thread_index == 0) {
		uint max_frame_count = 256;
		cloud_sample_count_volume[thread_id / 4] = min(history_frame_count + 1, max_frame_count);
	}
	
	float2 radiance_transfer = 0.0;
	
	[loop]
	for (uint i = 0; i < max_path_length; i += 1) {
		float noise_mip_level = min((float)i + scene.clouds.lighting_volume_noise_mip_offset, 7.0);
		VolumeInteractionType volume_interaction_type = SampleVolumetricMedium(ray_desc, 1024.0, hash, noise_mip_level);
		
		if (volume_interaction_type == VolumeInteractionType::Absorption) {
			i = max_path_length;
		} else if (volume_interaction_type == VolumeInteractionType::Scattering) {
			float3 wo = -ray_desc.Direction;
			float phase_function_g = scene.clouds.scattering_anisotropy;
			
			float phase_function = PhaseFunctionHG(dot(wo, scene.atmosphere.world_space_sun_direction), phase_function_g);
			
#if defined(USE_OPTICAL_DEPTH_VOLUME)
			float optical_depth = RaymarchHybridOpticalDepthRay(ray_desc.Origin, scene.atmosphere.world_space_sun_direction, noise_mip_level);
			float transmittance = exp(-optical_depth);
#else // !defined(USE_OPTICAL_DEPTH_VOLUME)
			float transmittance = TraceVolumetricMediumTransmittanceRay(ray_desc.Origin, scene.atmosphere.world_space_sun_direction, 1024.0, hash, noise_mip_level);
#endif // !defined(USE_OPTICAL_DEPTH_VOLUME)
			
			radiance_transfer.x += transmittance * phase_function;
			
			if (i + 1 == max_path_length) {
				//
				// Append the path from the previous frame to the end of the current frame path.
				//
				if (history_frame_count != 0) {
					//
					// Sample higher MIP levels when we don't have a lot of accumulated frames. In theory we should load the
					// history frame count at the the sample position, but it's good enough to use the current one.
					//
					float mip_level = history_frame_count < 32 ? 2.0 : (history_frame_count < 64 ? 1.0 : 0.0);
					
					float3 sample_uvw = (ray_desc.Origin - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size;
					radiance_transfer += max(cloud_radiance_transfer_volume_1.SampleLevel(sampler_linear_clamp, sample_uvw, mip_level), 0.0);
				} else {
					//
					// If we don't have any history samples, jump start convergence from a reasonable initial estimate.
					//
					radiance_transfer += float2(phase_function * exp(-optical_depth * 0.05), 0.25);
				}
			} else {
				ray_desc.Direction = SamplePhaseFunctionHG(wo, phase_function_g, ComputeRandomUnorm16x2(hash));
			}
		} else {
			radiance_transfer.y += max(ray_desc.Direction.z * 2.0, 0.0);
			i = max_path_length;
		}
	}
	
	float accumulation_ratio = 1.0 / (history_frame_count + 1);
	float2 old_accumulated_radiance_transfer = max(cloud_radiance_transfer_volume_1[thread_id], 0.0);
	float2 new_accumulated_radiance_transfer = max(lerp(old_accumulated_radiance_transfer, radiance_transfer, accumulation_ratio), 0.0);
	
	// Dither to prevent energy loss with very low accumulation ratio.
	cloud_radiance_transfer_volume_0_0[thread_id] = DitherFloat16(max(new_accumulated_radiance_transfer, 0.0), ComputeRandomUnorm16x2(hash));
	
	
	new_accumulated_radiance_transfer += WaveShuffleXor(new_accumulated_radiance_transfer, 0x1);
	new_accumulated_radiance_transfer += WaveShuffleXor(new_accumulated_radiance_transfer, 0x4);
	new_accumulated_radiance_transfer += WaveShuffleXor(new_accumulated_radiance_transfer, 0x10);
	new_accumulated_radiance_transfer *= (1.0 / 8.0);
	
	if ((thread_index & 0b101010) == thread_index) {
		cloud_radiance_transfer_volume_0_1[thread_id / 2] = DitherFloat16(new_accumulated_radiance_transfer, ComputeRandomUnorm16x2(hash));
		
		uint octant_index = ((thread_index >> 1) & 0x1) | ((thread_index >> 2) & 0x2) | ((thread_index >> 3) & 0x4);
		gs_radiance_transfer[octant_index] = new_accumulated_radiance_transfer;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < 8) {
		new_accumulated_radiance_transfer = gs_radiance_transfer[thread_index];
		new_accumulated_radiance_transfer += WaveShuffleXor(new_accumulated_radiance_transfer, 0x1);
		new_accumulated_radiance_transfer += WaveShuffleXor(new_accumulated_radiance_transfer, 0x2);
		new_accumulated_radiance_transfer += WaveShuffleXor(new_accumulated_radiance_transfer, 0x4);
		new_accumulated_radiance_transfer *= (1.0 / 8.0);
		
		if (thread_index == 0) {
			cloud_radiance_transfer_volume_0_2[thread_id / 4] = DitherFloat16(new_accumulated_radiance_transfer, ComputeRandomUnorm16x2(hash));
		}
	}
}
#endif // defined(RADIANCE_TRANSFER_VOLUME)
