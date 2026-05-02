#include "Basic.hlsl"
#include "AtmosphereSampling.hlsl"
#include "Generated/SceneData.hlsl"
#include "Generated/AtmosphereData.hlsl"

//
// Sebastien Hillaire. 2020. A Scalable and Production Ready Sky and Atmosphere Rendering Technique.
// https://github.com/sebh/UnrealEngineSkyAtmosphere see license in THIRD_PARTY_LICENSES.md
//

struct AtmosphereMedium {
	float3 scattering_mie;
	float3 absorption_mie;
	float3 extinction_mie;
	
	float3 scattering_rayleigh;
	float3 absorption_rayleigh;
	float3 extinction_rayleigh;
	
	float3 scattering_ozone;
	float3 absorption_ozone;
	float3 extinction_ozone;
	
	float3 scattering;
	float3 absorption;
	float3 extinction;
};

AtmosphereMedium SampleAtmosphereMedium(AtmosphereParameters atmosphere, float3 world_space_position) {
	float view_height = length(world_space_position) - atmosphere.bottom_radius;
	
	float density_mie      = exp(atmosphere.mie_density_exp_scale * view_height);
	float density_rayleigh = exp(atmosphere.rayleigh_density_exp_scale * view_height);
	uint ozone_layer_index = view_height < atmosphere.ozone_density_layer_height ? 0 : 1;
	float density_ozone    = saturate(atmosphere.ozone_density_scale[ozone_layer_index] * view_height + atmosphere.ozone_density_offset[ozone_layer_index]);
	
	AtmosphereMedium sample;
	sample.scattering_mie = density_mie * atmosphere.mie_scattering;
	sample.absorption_mie = density_mie * atmosphere.mie_absorption;
	sample.extinction_mie = sample.scattering_mie + sample.absorption_mie;
	
	sample.scattering_rayleigh = density_rayleigh * atmosphere.rayleigh_scattering;
	sample.absorption_rayleigh = 0.0;
	sample.extinction_rayleigh = sample.scattering_rayleigh + sample.absorption_rayleigh;
	
	sample.scattering_ozone = 0.0;
	sample.absorption_ozone = density_ozone * atmosphere.ozone_absorption;
	sample.extinction_ozone = sample.scattering_ozone + sample.absorption_ozone;
	
	sample.scattering = sample.scattering_mie + sample.scattering_rayleigh + sample.scattering_ozone;
	sample.absorption = sample.absorption_mie + sample.absorption_rayleigh + sample.absorption_ozone;
	sample.extinction = sample.extinction_mie + sample.extinction_rayleigh + sample.extinction_ozone;
	
	return sample;
}

float3 ComputeScatteringIntegral(float3 slice_scattering, float3 slice_transmittance, float3 slice_extinction) {
	// Integrate[Power[e, -x*a], {x, 0, l}]
	float3 safe_slice_extinction = max(slice_extinction, 1e-6);
	return slice_scattering * (1.0 - slice_transmittance) / safe_slice_extinction;
}


#if defined(TRANSMITTANCE_LUT)
float3 IntegrateTransmittance(AtmosphereParameters atmosphere, float3 planet_space_position, float3 planet_space_direction) {
	float t_max = RayAtmosphereIntersect(planet_space_position, planet_space_direction, atmosphere.top_radius);
	
	compile_const float sample_count        = 40.0;
	compile_const float sample_index_offset = 0.3;
	
	float curr_t  = 0.0;
	float delta_t = t_max / sample_count;
	
	float3 optical_depth = 0.0;
	for (float sample_index = 0.0; sample_index < sample_count; sample_index += 1.0) {
		float new_t = t_max * (sample_index + sample_index_offset) * rcp(sample_count);
		delta_t = new_t - curr_t;
		curr_t  = new_t;
		
		float3 position = planet_space_position + curr_t * planet_space_direction;
		
		AtmosphereMedium medium = SampleAtmosphereMedium(atmosphere, position);
		optical_depth += medium.extinction * delta_t;
	}
	
	return exp(-optical_depth);
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * inv_transmittance_lut_size;
	
	TransmittanceLutCoordinates coordinates = UvToTransmittanceLutCoordinates(atmosphere, thread_uv);
	
	float3 planet_space_position  = float3(0.0, 0.0, coordinates.view_height);
	float3 planet_space_direction = float3(0.0, sqrt(1.0 - Pow2(coordinates.cos_view_zenith_angle)), coordinates.cos_view_zenith_angle);
	
	float3 transmittance = IntegrateTransmittance(atmosphere, planet_space_position, planet_space_direction);
	transmittance_lut[thread_id] = float4(transmittance, 1.0);
}
#endif // defined(TRANSMITTANCE_LUT)

#if defined(MULTIPLE_SCATTERING_LUT)
struct MultipleScatteringValues {
	float3 multiple_scattering;
	float3 scattered_radiance ;
};

MultipleScatteringValues IntegrateMultipleScattering(AtmosphereParameters atmosphere, float3 planet_space_position, float3 planet_space_direction, float3 planet_space_sun_direction) {
	float t_bottom = RayAtmosphereIntersect(planet_space_position, planet_space_direction, atmosphere.bottom_radius);
	float t_top = RayAtmosphereIntersect(planet_space_position, planet_space_direction, atmosphere.top_radius);
	float t_max = t_bottom >= 0.0 ? t_bottom : t_top;
	
	compile_const float sample_count        = 20.0;
	compile_const float sample_index_offset = 0.3;
	
	float curr_t  = 0.0;
	float delta_t = t_max / sample_count;
	
	MultipleScatteringValues result;
	result.multiple_scattering = 0.0;
	result.scattered_radiance  = 0.0;
	
	float3 transmittance = 1.0;
	for (float s = 0.0; s < sample_count; s += 1.0) {
		float new_t = t_max * (s + sample_index_offset) / sample_count;
		delta_t = new_t - curr_t;
		curr_t = new_t;
		
		float3 position = planet_space_position + curr_t * planet_space_direction;
		
		AtmosphereMedium medium = SampleAtmosphereMedium(atmosphere, position);
		float3 slice_transmittance = exp(-medium.extinction * delta_t);
		
		TransmittanceLutCoordinates coordinates;
		coordinates.view_height = length(position);
		float3 up_vector = position / coordinates.view_height;
		coordinates.cos_view_zenith_angle = dot(planet_space_sun_direction, up_vector);
		
		float2 transmittance_lut_uv = TransmittanceLutCoordinatesToUv(atmosphere, coordinates);
		float3 transmittance_to_light = transmittance_lut.SampleLevel(sampler_linear_clamp, transmittance_lut_uv, 0);
		
		float3 slice_scattering = transmittance_to_light * medium.scattering * (1.0 / (4.0 * PI));
		result.multiple_scattering += ComputeScatteringIntegral(medium.scattering, slice_transmittance, medium.extinction) * transmittance;
		result.scattered_radiance  += ComputeScatteringIntegral(slice_scattering,  slice_transmittance, medium.extinction) * transmittance;
		
		transmittance *= slice_transmittance;
	}
	
#if 0
	if (t_max == t_bottom && t_bottom > 0.0) {
		// Account for light bounced off the ground.
		float3 position = planet_space_position + t_bottom * planet_space_direction;
		
		TransmittanceLutCoordinates coordinates;
		coordinates.view_height = length(position);
		float3 up_vector = position / coordinates.view_height;
		coordinates.cos_view_zenith_angle = dot(planet_space_sun_direction, up_vector);
		
		float2 transmittance_lut_uv = TransmittanceLutCoordinatesToUv(atmosphere, coordinates);
		float3 transmittance_to_light = transmittance_lut.SampleLevel(sampler_linear_clamp, transmittance_lut_uv, 0);
		
		float n_dot_l = saturate(dot(normalize(up_vector), normalize(planet_space_sun_direction)));
		
		compile_const float3 ground_albedo = 0.0;
		result.scattered_radiance += transmittance_to_light * transmittance * n_dot_l * ground_albedo * (1.0 / PI);
	}
#endif
	
	return result;
}

compile_const uint sqrt_sample_count = 8;
compile_const uint sample_count      = sqrt_sample_count * sqrt_sample_count;

compile_const uint min_wave_size = 4;
groupshared MultipleScatteringValues gs_multiple_scattering_values[sample_count / min_wave_size];

[ThreadGroupSize(sample_count, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	float2 group_uv = (group_id + 0.5) * inv_multiple_scattering_lut_size;
	
	float cos_sun_zenith_angle = group_uv.x * 2.0 - 1.0;
	float3 planet_space_sun_direction = float3(0.0, sqrt(saturate(1.0 - cos_sun_zenith_angle * cos_sun_zenith_angle)), cos_sun_zenith_angle);
	float view_height = atmosphere.bottom_radius + saturate(group_uv.y + planet_radius_offset) * (atmosphere.top_radius - atmosphere.bottom_radius - planet_radius_offset);
	
	float2 rand = (MortonDecode(thread_index) + 0.5) * rcp(sqrt_sample_count);
	float theta = 2.0 * PI * rand.x;
	float phi   = acos(rand.y * 2.0 - 1.0);
	
	float sin_phi   = sin(phi);
	float cos_phi   = cos(phi);
	float sin_theta = sin(theta);
	float cos_theta = cos(theta);
	
	float3 planet_space_position  = float3(0.0, 0.0, view_height);
	float3 planet_space_direction = float3(cos_theta * sin_phi, sin_theta * sin_phi, cos_phi);
	
	MultipleScatteringValues result = IntegrateMultipleScattering(atmosphere, planet_space_position, planet_space_direction, planet_space_sun_direction);
	result.multiple_scattering = WaveActiveSum(result.multiple_scattering);
	result.scattered_radiance  = WaveActiveSum(result.scattered_radiance);
	
	if (WaveIsFirstLane()) {
		gs_multiple_scattering_values[thread_index / WaveGetLaneCount()] = result;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		uint wave_count = sample_count / WaveGetLaneCount();
		
		for (uint i = 1; i < wave_count; i += 1) {
			result.multiple_scattering += gs_multiple_scattering_values[i].multiple_scattering;
			result.scattered_radiance  += gs_multiple_scattering_values[i].scattered_radiance;
		}
		
		float3 multiple_scattering = result.multiple_scattering * rcp(sample_count);
		float3 scattered_radiance  = result.scattered_radiance  * rcp(sample_count);
		
		float3 sum_of_all_multiple_scattering_events_contribution = 1.0 / (1.0 - multiple_scattering);
		multiple_scattering_lut[group_id] = float4(scattered_radiance * sum_of_all_multiple_scattering_events_contribution, 1.0);
	}
}
#endif // defined(MULTIPLE_SCATTERING_LUT)


#if defined(SKY_PANORAMA_LUT)
float RayleighPhase(float cos_theta) {
	float factor = 3.0 / (16.0 * PI);
	return factor * (1.0 + cos_theta * cos_theta);
}

float CornetteShanksMiePhaseFunction(float g, float cos_theta) {
	float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + cos_theta * cos_theta) / pow(1.0 + g * g - 2.0 * g * -cos_theta, 1.5);
}

float3 SampleMultipleScatteringLut(AtmosphereParameters atmosphere, float3 planet_space_position, float cos_view_zenith_angle) {
	float2 uv;
	uv.x = cos_view_zenith_angle * 0.5 + 0.5;
	uv.y = (length(planet_space_position) - atmosphere.bottom_radius) / (atmosphere.top_radius - atmosphere.bottom_radius);
	
	return multiple_scattering_lut.SampleLevel(sampler_linear_clamp, uv, 0).xyz;
}

float3 IntegrateScattering(AtmosphereParameters atmosphere, float3 planet_space_position, float3 planet_space_direction, float3 planet_space_sun_direction) {
	float t_bottom = RayAtmosphereIntersect(planet_space_position, planet_space_direction, atmosphere.bottom_radius);
	float t_top = RayAtmosphereIntersect(planet_space_position, planet_space_direction, atmosphere.top_radius);
	float t_max = t_bottom >= 0.0 ? t_bottom : t_top;
	
	compile_const float sample_count        = 30.0;
	compile_const float sample_index_offset = 0.3;
	
	float curr_t  = 0.0;
	float delta_t = t_max / sample_count;
	
	// Negate cos_theta because due to planet_space_direction being a "in" direction.
	float cos_theta = -dot(planet_space_sun_direction, planet_space_direction);
	float mie_phase_value      = CornetteShanksMiePhaseFunction(atmosphere.mie_phase_g, cos_theta);
	float rayleigh_phase_value = RayleighPhase(cos_theta);
	
	float3 scattered_radiance = 0.0;
	
	float3 transmittance = 1.0;
	for (float s = 0.0; s < sample_count; s += 1.0) {
		float new_t = t_max * (s + sample_index_offset) / sample_count;
		delta_t = new_t - curr_t;
		curr_t = new_t;
		
		float3 position = planet_space_position + curr_t * planet_space_direction;
		
		AtmosphereMedium medium = SampleAtmosphereMedium(atmosphere, position);
		float3 slice_transmittance = exp(-medium.extinction * delta_t);
		
		TransmittanceLutCoordinates coordinates;
		coordinates.view_height = length(position);
		float3 up_vector = position / coordinates.view_height;
		coordinates.cos_view_zenith_angle = dot(planet_space_sun_direction, up_vector);
		
		float2 transmittance_lut_uv = TransmittanceLutCoordinatesToUv(atmosphere, coordinates);
		float3 transmittance_to_light = transmittance_lut.SampleLevel(sampler_linear_clamp, transmittance_lut_uv, 0);
		
		// Planet shadow.
		float t_planet = RayAtmosphereIntersect(position - planet_radius_offset * up_vector, planet_space_sun_direction, atmosphere.bottom_radius);
		float planet_shadow = t_planet >= 0.0 ? 0.0 : 1.0;
		
		float3 multiple_scattering = SampleMultipleScatteringLut(atmosphere, position, coordinates.cos_view_zenith_angle);
		
		float3 phase_times_scattering = planet_shadow * (medium.scattering_mie * mie_phase_value + medium.scattering_rayleigh * rayleigh_phase_value);
		float3 slice_scattering = transmittance_to_light * phase_times_scattering + multiple_scattering * medium.scattering;
		scattered_radiance += ComputeScatteringIntegral(slice_scattering, slice_transmittance, medium.extinction) * transmittance;
		
		transmittance *= slice_transmittance;
	}
	
	return scattered_radiance;
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * inv_sky_panorama_lut_size;
	
	float3 planet_space_position = TransformWorldToPlanetSpace(scene.world_space_camera_position, atmosphere.bottom_radius);
	float  view_height = length(planet_space_position);
	
	SkyPanoramaLutCoordinates coordinates = UvToSkyPanoramaLutCoordinates(atmosphere, view_height, thread_uv);
	
	float3 up_vector = planet_space_position / view_height;
	float cos_sun_zenith_angle = dot(up_vector, atmosphere.world_space_sun_direction);
	float3 planet_space_sun_direction = normalize(float3(sqrt(1.0 - cos_sun_zenith_angle * cos_sun_zenith_angle), 0.0, cos_sun_zenith_angle));
	
	planet_space_position = float3(0.0, 0.0, view_height);
	
	float sin_view_zenith_angle = sqrt(1.0 - coordinates.cos_view_zenith_angle * coordinates.cos_view_zenith_angle);
	
	float3 planet_space_direction;
	planet_space_direction.x = sin_view_zenith_angle * coordinates.cos_light_view_angle;
	planet_space_direction.y = sin_view_zenith_angle * sqrt(1.0 - coordinates.cos_light_view_angle * coordinates.cos_light_view_angle);
	planet_space_direction.z = coordinates.cos_view_zenith_angle;
	
	float3 scattering = IntegrateScattering(atmosphere, planet_space_position, planet_space_direction, planet_space_sun_direction);
	
	sky_panorama_lut[thread_id] = float4(scattering * atmosphere.sun_irradiance, 1.0);
}
#endif // defined(SKY_PANORAMA_LUT)


#if defined(ATMOSPHERE_COMPOSITE)
[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth != 0.0) return;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	float3 planet_space_direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	
	float3 sky_radiance = SampleSkyPanoramaLUT(atmosphere, sky_panorama_lut, transmittance_lut, scene.world_space_camera_position, planet_space_direction);
	
	scene_radiance[thread_id] = float4(sky_radiance, 1.0);
	
	// Camera rotation and FOV change motion vectors for the sky. Camera translation is not accounted for since sky is at an infinite distance.
	if (IsPerspectiveMatrix(scene.clip_to_view_coef)) {
		float2 curr_ndc = ScreenUvToNdc((thread_id + 0.5) * scene.inv_render_target_size);
		
		float3 view_space_position      = TransformNdcToViewSpace(curr_ndc, 1.0, scene.clip_to_view_coef);
		float3 world_space_position     = mul((float3x3)scene.view_to_world,      view_space_position);
		float3 prev_view_space_position = mul((float3x3)scene.prev_world_to_view, world_space_position);
		float4 prev_clip_space_position = TransformViewToClipSpace(prev_view_space_position, scene.prev_view_to_clip_coef);
		
		float2 prev_ndc = prev_clip_space_position.xy / prev_clip_space_position.w;
		
		motion_vectors[thread_id] = NdcToScreenUvDirection(prev_ndc - curr_ndc);
	}
}
#endif // defined(ATMOSPHERE_COMPOSITE)

