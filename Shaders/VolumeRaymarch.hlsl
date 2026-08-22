#include "Basic.hlsl"
#if !defined(CLOUD_SHADOW_MAP_FILTER)
#include "AtmosphereSampling.hlsl"
#include "BrdfSampling.hlsl"
#include "CloudSampling.hlsl"
#include "VolumeSampling.hlsl"
#endif // !defined(CLOUD_SHADOW_MAP_FILTER)

compile_const float hybrid_transmittance_ray_length = 8.0;

#if defined(VOLUME_RAYMARCH) || defined(CLOUD_RADIANCE_TRANSFER_VOLUME)
float RaymarchHybridOpticalDepthRay(float3 origin, float3 direction, float noise_mip_level) {
	float optical_depth = 0.0;
	
	float sample_count = 3.0;
	
	float prev_ray_t = 0.0;
	for (float t = (1.0 / sample_count); t < 1.0; t += (1.0 / sample_count)) {
		float ray_t = Pow2(t) * hybrid_transmittance_ray_length;
		
		float3 position = direction * ray_t + origin;
		float density = ComputeCloudMediumDensity(position, noise_mip_level);
		
		float extinction_coefficients = density;
		optical_depth += extinction_coefficients * (ray_t - prev_ray_t);
		prev_ray_t = ray_t;
	}
	
	float3 sample_uvw = (origin - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size;
	float cloud_optical_depth = cloud_optical_depth_volume.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
	
	return optical_depth * scene.clouds.extinction_coefficients + cloud_optical_depth;
}
#endif // defined(VOLUME_RAYMARCH) || defined(CLOUD_RADIANCE_TRANSFER_VOLUME)

#if defined(VOLUME_RAYMARCH) || defined(CLOUD_OPTICAL_DEPTH_VOLUME) || defined(CLOUD_SHADOW_MAP)
void SkipEmptySpaceInTwoDirections(RayDesc ray_desc, inout VolumetricSceneIntersection intersection) {
	VoxelTraversalState forward_traversal_state = BeginVoxelTraversal(ray_desc.Origin, ray_desc.Direction, intersection.t_max);
	intersection.t_min = VoxelGridSkipEmptySpace(forward_traversal_state, ray_desc.Direction, intersection.t_min);
	
	VoxelTraversalState backward_traversal_state = BeginVoxelTraversal(ray_desc.Origin + ray_desc.Direction * intersection.t_max, -ray_desc.Direction, intersection.t_max - intersection.t_min);
	intersection.t_max -= VoxelGridSkipEmptySpace(backward_traversal_state, -ray_desc.Direction, 0.0);
	
	intersection.is_hit = (intersection.t_min < intersection.t_max);
}
#endif // defined(VOLUME_RAYMARCH) || defined(CLOUD_OPTICAL_DEPTH_VOLUME) || defined(CLOUD_SHADOW_MAP)

#if defined(VOLUME_RAYMARCH)
[ThreadGroupSize(256, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * 16 + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	
	float depth = depth_stencil[thread_id];
	float ray_t_max = length(TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc));
	
	VolumetricSceneIntersection intersection = (VolumetricSceneIntersection)0;
	VolumetricSceneIntersection clouds_intersection = (VolumetricSceneIntersection)0;
	
	if (scene.feature_flags & SceneFeatureFlags::Clouds) {
		clouds_intersection = RayBoxIntersection(ray_desc.Origin - scene.clouds.world_space_position, ray_desc.Direction, scene.clouds.world_space_size, ray_t_max);
		SkipEmptySpaceInTwoDirections(ray_desc, clouds_intersection);
		
		intersection = clouds_intersection;
	}
	
	if (scene.feature_flags & SceneFeatureFlags::Fog) {
		VolumetricSceneIntersection fog_intersection = RayBoxIntersection(ray_desc.Origin - scene.fog.world_space_position, ray_desc.Direction, scene.fog.world_space_size, ray_t_max);
		if (intersection.is_hit) {
			intersection.t_min = min(intersection.t_min, fog_intersection.t_min);
			intersection.t_max = max(intersection.t_max, fog_intersection.t_max);
		} else {
			intersection = fog_intersection;
		}
	}
	
	float3 scattering = 0.0;
	float transmittance = 1.0;
	
	float cloud_t_min  = intersection.t_max;
	float sample_count = 0.0;
	
	float noise   = LoadBlueNoise(blue_noise_1d, thread_id, scene.frame_index);
	float delta_t = (clouds_intersection.is_hit ? 8.0 : 32.0);
	float ray_t   = intersection.t_min + delta_t * noise;
	
	for (; (ray_t < intersection.t_max) && (transmittance >= CloudConstants::transmittance_threshold); ray_t += delta_t, sample_count += 1.0) {
		float3 position = ray_desc.Direction * ray_t + ray_desc.Origin;
		float noise_mip_level = (float)min(FloorLog2(max(ray_t * scene.clouds.raymarch_noise_mip_scale, 1.0)), 7);
		
		float cloud_density = 0.0;
		if ((ray_t >= clouds_intersection.t_min) && (ray_t <= clouds_intersection.t_max)) {
			cloud_density = ComputeCloudMediumDensity(position, noise_mip_level);
		}
		float fog_density = ComputeFogMediumDensity(position.z);
		
		float cloud_scattering_coefficients = scene.clouds.scattering_coefficients * cloud_density;
		float fog_scattering_coefficients   = scene.fog.scattering_coefficients    * fog_density;
		float extinction_coefficients       = scene.clouds.extinction_coefficients * cloud_density + scene.fog.extinction_coefficients * fog_density;
		
		if (extinction_coefficients >= CloudConstants::extinction_coefficients_threshold) {
			float cloud_sun_optical_depth = 0.0;
			if (cloud_density > 0.0) {
				cloud_sun_optical_depth = RaymarchHybridOpticalDepthRay(position, scene.atmosphere.world_space_sun_direction, noise_mip_level);
			}
			
			float fog_sun_optical_depth = 0.0;
			if (fog_density > 0.0) {
				float3 shadow_view_space_position = mul(scene.clouds.world_to_view, float4(position, 1.0));
				float4 shadow_clip_space_position = TransformViewToClipSpace(shadow_view_space_position, scene.clouds.view_to_clip_coef);
				float2 shadow_uv_position = NdcToScreenUv(shadow_clip_space_position.xy);
				
				if (all(shadow_uv_position >= 0) && all(shadow_uv_position <= 1.0)) {
					float3 shadow_map = cloud_shadow_map.SampleLevel(sampler_linear_clamp, shadow_uv_position, 0.0);
					
					// During filtering the ordering of min/max depth might get swapped.
					float min_cloud_depth = min(shadow_map.x, shadow_map.y);
					float max_cloud_depth = max(shadow_map.x, max(shadow_map.y, min_cloud_depth + (1.0 / 1024.0)));
					fog_sun_optical_depth = smoothstep(max_cloud_depth, min_cloud_depth, max(shadow_view_space_position.z, 0.0)) * shadow_map.z;
				}
			}
			
			float2 cloud_radiance_transfer = 0.0;
			if (cloud_density > 0.0) {
				float3 sample_uvw = (position - scene.clouds.world_space_position) * scene.clouds.inv_world_space_size;
				cloud_radiance_transfer = cloud_radiance_transfer_volume.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
			}
			
			float2 fog_radiance_transfer = 0.0;
			if (fog_density > 0.0) {
				float3 sample_uvw = (position - scene.fog.world_space_position) * scene.fog.inv_world_space_size;
				fog_radiance_transfer = fog_radiance_transfer_volume.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
			}
			
			float slice_transmittance = exp(-extinction_coefficients * delta_t);
			float scattering_integral = transmittance * (1.0 - slice_transmittance) / extinction_coefficients;
			float cloud_scattering_integral = cloud_scattering_coefficients * scattering_integral;
			float fog_scattering_integral   = fog_scattering_coefficients   * scattering_integral;
			
			scattering.x += exp(-cloud_sun_optical_depth) * cloud_scattering_integral + exp(-fog_sun_optical_depth) * fog_scattering_integral;
			scattering.y += cloud_radiance_transfer.x     * cloud_scattering_integral + fog_radiance_transfer.x     * fog_scattering_integral;
			scattering.z += cloud_radiance_transfer.y     * cloud_scattering_integral + fog_radiance_transfer.y     * fog_scattering_integral;
			transmittance *= slice_transmittance;
			
			if (cloud_density > 0.0) {
				cloud_t_min = min(cloud_t_min, ray_t);
			}
		}
	}
	
	scattering.x *= PhaseFunctionDualHG(dot(-ray_desc.Direction, scene.atmosphere.world_space_sun_direction), scene.clouds.dual_hg_parameters);
	
	float3 sky_irradiance = average_sky_irradiance[uint2(0, 0)];
	float3 sun_irradiance = scene.atmosphere.sun_color * scene.atmosphere.sun_irradiance * SampleTransmittanceLUT(scene.atmosphere, transmittance_lut, ray_desc.Direction * cloud_t_min + ray_desc.Origin);
	
	float3 radiance = sun_irradiance * scattering.x + sun_irradiance * scattering.y + scattering.z * sky_irradiance;
	scene_radiance[thread_id] = float4(scene_radiance[thread_id].xyz * transmittance + radiance * scene.exposure_estimate, 1.0);
}
#endif // defined(VOLUME_RAYMARCH)


#if defined(CLOUD_OPTICAL_DEPTH_VOLUME)
float RaymarchOpticalDepthRay(float3 origin, float3 direction, float t_max) {
	float optical_depth = 0.0;
	float noise_mip_level = scene.clouds.lighting_volume_noise_mip_offset;
	
	float delta_t = 4.0;
	for (float ray_t = hybrid_transmittance_ray_length; ray_t < t_max; ray_t += delta_t, noise_mip_level += 1.0) {
		float3 position = direction * ray_t + origin;
		float density = ComputeCloudMediumDensity(position, min(noise_mip_level, 7.0));
		
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
#endif // defined(CLOUD_OPTICAL_DEPTH_VOLUME)


#if defined(CLOUD_SHADOW_MAP)
float3 RaymarchBeerShadowMapRay(float3 origin, float3 direction, float t_min, float t_max) {
	float noise_mip_level = scene.clouds.lighting_volume_noise_mip_offset;
	
	float optical_depth = 0.0;
	float cloud_t_max   = t_min;
	float cloud_t_min   = t_max;
	
	float delta_t = 4.0;
	for (float ray_t = t_min; ray_t < t_max; ray_t += delta_t, noise_mip_level += 1.0) {
		float3 position = direction * ray_t + origin;
		float density = ComputeCloudMediumDensity(position, min(noise_mip_level, 7.0));
		
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
#endif // defined(CLOUD_SHADOW_MAP)


#if defined(CLOUD_SHADOW_MAP_FILTER)
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
#endif // defined(CLOUD_SHADOW_MAP_FILTER)

#if defined(CLOUD_RADIANCE_TRANSFER_VOLUME) || defined(FOG_RADIANCE_TRANSFER_VOLUME)
RayDesc CreatePrimaryRay(float3 world_space_position, inout uint hash) {
	// Use isotropic phase function on the first scattering vertex to make sure radiance transfer is view independent.
	// This also allows us to sample the radiance transfer volume for the last vertex on the path to get more bounces.
	RayDesc ray_desc;
	ray_desc.Origin    = world_space_position;
	ray_desc.Direction = SphereMapping(ComputeRandomUnorm16x2(hash));
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = 1024.0;
	
	return ray_desc;
}
#endif // defined(CLOUD_RADIANCE_TRANSFER_VOLUME) || defined(FOG_RADIANCE_TRANSFER_VOLUME)


#if defined(CLOUD_RADIANCE_TRANSFER_VOLUME)
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
	
	RayDesc ray_desc = CreatePrimaryRay(world_space_position, hash);
	
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
			
			float phase_function = PhaseFunctionDualHG(dot(wo, scene.atmosphere.world_space_sun_direction), scene.clouds.dual_hg_parameters);
			
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
				ray_desc.Direction = SamplePhaseFunctionDualHG(wo, scene.clouds.dual_hg_parameters, ComputeRandomUnorm16x2(hash));
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
#endif // defined(CLOUD_RADIANCE_TRANSFER_VOLUME)

#if defined(FOG_RADIANCE_TRANSFER_VOLUME)
#include "VolumeSampling.hlsl"

[ThreadGroupSize(4, 4, 4)]
void MainCS(uint3 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint3 thread_id = group_id * 4 + RowMajorDecode3D(thread_index, 2);
	
	uint hash = WyHash32(RowMajorEncode3D(thread_id, 10), scene.frame_index);
	
	float3 thread_uv = (thread_id + ComputeRandomUnorm10x3(hash)) / FogConstants::lighting_volume_size;
	float3 world_space_position = thread_uv * scene.fog.world_space_size + scene.fog.world_space_position;
	
	// uint max_path_length = 32;
	uint max_path_length = 4;
	
	RayDesc ray_desc = CreatePrimaryRay(world_space_position, hash);
	
	float2 radiance_transfer = 0.0;
	
	[loop]
	for (uint i = 0; i < max_path_length; i += 1) {
		VolumeInteractionType volume_interaction_type = SampleVolumetricMedium(ray_desc, 1024.0, hash, 7.0);
		
		if (volume_interaction_type == VolumeInteractionType::Absorption) {
			i = max_path_length;
		} else if (volume_interaction_type == VolumeInteractionType::Scattering) {
			float3 wo = -ray_desc.Direction;
			
			float phase_function = PhaseFunctionDualHG(dot(wo, scene.atmosphere.world_space_sun_direction), scene.clouds.dual_hg_parameters);
			float transmittance = TraceVolumetricMediumTransmittanceRay(ray_desc.Origin, scene.atmosphere.world_space_sun_direction, 1024.0, hash, 7.0);
			
			radiance_transfer.x += transmittance * phase_function;
			
			if (i + 1 == max_path_length) {
				// Append the path from the previous frame to the end of the current frame path.
				float3 sample_uvw = (ray_desc.Origin - scene.fog.world_space_position) * scene.fog.inv_world_space_size;
				radiance_transfer += max(fog_radiance_transfer_volume_1.SampleLevel(sampler_linear_clamp, sample_uvw, 0.0), 0.0);
			} else {
				ray_desc.Direction = SamplePhaseFunctionDualHG(wo, scene.clouds.dual_hg_parameters, ComputeRandomUnorm16x2(hash));
			}
		} else {
			radiance_transfer.y += max(ray_desc.Direction.z * 2.0, 0.0);
			i = max_path_length;
		}
	}
	
	float accumulation_ratio = 1.0 / min(scene.frame_index, 1024);
	float2 old_accumulated_radiance_transfer = max(fog_radiance_transfer_volume_1[thread_id], 0.0);
	float2 new_accumulated_radiance_transfer = max(lerp(old_accumulated_radiance_transfer, radiance_transfer, accumulation_ratio), 0.0);
	
	// Dither to prevent energy loss with very low accumulation ratio.
	fog_radiance_transfer_volume_0[thread_id] = DitherFloat16(max(new_accumulated_radiance_transfer, 0.0), ComputeRandomUnorm16x2(hash));
}
#endif // defined(CLOUD_RADIANCE_TRANSFER_VOLUME)
