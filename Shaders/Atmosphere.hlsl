#include "Basic.hlsl"
#include "Generated/SceneData.hlsl"
#include "Generated/AtmosphereData.hlsl"

//
// Sebastien Hillaire. 2020. A Scalable and Production Ready Sky and Atmosphere Rendering Technique.
// https://github.com/sebh/UnrealEngineSkyAtmosphere see license in THIRD_PARTY_LICENSES.md
//

compile_const float2 inv_transmittance_lut_size       = 1.0 / AtmosphereParameters::transmittance_lut_size;
compile_const float2 inv_multiple_scattering_lut_size = 1.0 / AtmosphereParameters::multiple_scattering_lut_size;
compile_const float2 inv_sky_panorama_lut_size        = 1.0 / AtmosphereParameters::sky_panorama_lut_size;
compile_const float  planet_radius_offset             = 0.01;
compile_const uint   thread_group_size                = AtmosphereParameters::thread_group_size;

compile_const AtmosphereParameters default_atmosphere = {
	6360.0, // bottom_radius, km
	6460.0, // top_radius, km
	
	-1.0 / 8.0, // rayleigh_density_exp_scale
	float3(0.005802, 0.013558, 0.033100), // rayleigh_scattering, 1/km
	
	-1.0 / 1.2, // mie_density_exp_scale
	float3(0.003996, 0.003996, 0.003996), // mie_scattering, 1/km
	float3(0.000444, 0.000444, 0.000444), // mie_absorption, 1/km
	0.8, // mie_phase_g
	
	25.0, // ozone_density_layer_height, km
	float2( 1.0 / 15.0, -1.0 / 15.0), // ozone_density_scale
	float2(-2.0 /  3.0,  8.0 /  3.0), // ozone_density_offset
	float3(0.000650, 0.001881, 0.000085), // ozone_absorption
};


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


struct TransmittanceLutCoordinates {
	float view_height;
	float cos_view_zenith_angle;
};

TransmittanceLutCoordinates UvToTransmittanceLutCoordinates(AtmosphereParameters atmosphere, float2 uv) {
	float x_mu = uv.x;
	float x_r  = uv.y;
	
	float H   = sqrt(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius);
	float rho = H * x_r;
	
	TransmittanceLutCoordinates coordinates;
	coordinates.view_height = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);
	
	float d_min = atmosphere.top_radius - coordinates.view_height;
	float d_max = rho + H;
	float d     = d_min + x_mu * (d_max - d_min);
	
	coordinates.cos_view_zenith_angle = clamp(d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * coordinates.view_height * d), -1.0, +1.0);
	
	return coordinates;
}

float2 TransmittanceLutCoordinatesToUv(AtmosphereParameters atmosphere, TransmittanceLutCoordinates coordinates) {
	float H   = sqrt(max(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius, 0.0));
	float rho = sqrt(max(coordinates.view_height * coordinates.view_height - atmosphere.bottom_radius * atmosphere.bottom_radius, 0.0));
	
	float discriminant = coordinates.view_height * coordinates.view_height * (coordinates.cos_view_zenith_angle * coordinates.cos_view_zenith_angle - 1.0) + atmosphere.top_radius * atmosphere.top_radius;
	float d = max(-coordinates.view_height * coordinates.cos_view_zenith_angle + sqrt(discriminant), 0.0); // Distance to atmosphere boundary.
	
	float d_min = atmosphere.top_radius - coordinates.view_height;
	float d_max = rho + H;
	float x_mu  = (d - d_min) / (d_max - d_min);
	float x_r   = rho / H;
	
	return float2(x_mu, x_r);
}


struct SkyPanoramaLutCoordinates {
	float cos_view_zenith_angle;
	float cos_light_view_angle;
};

SkyPanoramaLutCoordinates UvToSkyPanoramaLutCoordinates(AtmosphereParameters atmosphere, float view_height, float2 uv) {
	float vhorizon = sqrt(view_height * view_height - atmosphere.bottom_radius * atmosphere.bottom_radius);
	float cos_beta = vhorizon / view_height;
	float beta     = acos(cos_beta);
	float zenith_horizon_angle = PI - beta;
	
	SkyPanoramaLutCoordinates coordinates;
	
	if (uv.y < 0.5) {
		float coord = 1.0 - Pow2(1.0 - 2.0 * uv.y);
		coordinates.cos_view_zenith_angle = cos(zenith_horizon_angle * coord);
	} else {
		float coord = Pow2(uv.y * 2.0 - 1.0);
		coordinates.cos_view_zenith_angle = cos(zenith_horizon_angle + beta * coord);
	}
	
	coordinates.cos_light_view_angle = 1.0 - Pow2(uv.x) * 2.0;
	
	return coordinates;
}

float2 SkyPanoramaLutCoordinatesToUv(AtmosphereParameters atmosphere, bool intersect_ground, SkyPanoramaLutCoordinates coordinates, float view_height) {
	float vhorizon = sqrt(view_height * view_height - atmosphere.bottom_radius * atmosphere.bottom_radius);
	float cos_beta = vhorizon / view_height;
	float beta     = acos(cos_beta);
	float zenith_horizon_angle = PI - beta;
	
	float2 uv;
	if (intersect_ground == false) {
		float coord = 1.0 - sqrt(1.0 - (acos(coordinates.cos_view_zenith_angle) / zenith_horizon_angle));
		uv.y = coord * 0.5;
	} else {
		float coord = sqrt((acos(coordinates.cos_view_zenith_angle) - zenith_horizon_angle) / beta);
		uv.y = coord * 0.5 + 0.5;
	}
	uv.x = sqrt(0.5 - coordinates.cos_light_view_angle * 0.5);
	
	return uv;
}


float RaySphereIntersect(float3 ray_origin, float3 ray_direction, float radius) {
	float b = 2.0 * dot(ray_direction, ray_origin);
	float c = dot(ray_origin, ray_origin) - (radius * radius);
	float delta = b * b - 4.0 * c;
	
	if (delta < 0.0) return -1.0;
	
	float root_0 = (-b - sqrt(delta)) * 0.5;
	float root_1 = (-b + sqrt(delta)) * 0.5;
	
	if (root_0 < 0.0 && root_1 < 0.0) return -1.0;
	
	if (root_0 < 0.0) return max(root_1, 0.0);
	if (root_1 < 0.0) return max(root_0, 0.0);
	
	return max(min(root_0, root_1), 0.0);
}

float3 ComputeScatteringIntegral(float3 slice_scattering, float3 slice_transmittance, float3 slice_extinction) {
	// Integrate[Power[e, -x*a], {x, 0, l}]
	float3 safe_slice_extinction = max(slice_extinction, 1e-6);
	return slice_scattering * (1.0 - slice_transmittance) / safe_slice_extinction;
}


#if defined(TRANSMITTANCE_LUT)
float3 IntegrateTransmittance(AtmosphereParameters atmosphere, float3 planet_space_position, float3 planet_space_direction) {
	float t_max = RaySphereIntersect(planet_space_position, planet_space_direction, atmosphere.top_radius);
	
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
	
	TransmittanceLutCoordinates coordinates = UvToTransmittanceLutCoordinates(default_atmosphere, thread_uv);
	
	float3 planet_space_position  = float3(0.0, 0.0, coordinates.view_height);
	float3 planet_space_direction = float3(0.0, sqrt(1.0 - Pow2(coordinates.cos_view_zenith_angle)), coordinates.cos_view_zenith_angle);
	
	float3 transmittance = IntegrateTransmittance(default_atmosphere, planet_space_position, planet_space_direction);
	transmittance_lut[thread_id] = float4(transmittance, 1.0);
}
#endif // defined(TRANSMITTANCE_LUT)

#if defined(MULTIPLE_SCATTERING_LUT)
struct MultipleScatteringValues {
	float3 multiple_scattering;
	float3 scattered_radiance ;
};

MultipleScatteringValues IntegrateMultipleScattering(AtmosphereParameters atmosphere, float3 planet_space_position, float3 planet_space_direction, float3 planet_space_sun_direction) {
	float t_bottom = RaySphereIntersect(planet_space_position, planet_space_direction, atmosphere.bottom_radius);
	float t_top = RaySphereIntersect(planet_space_position, planet_space_direction, atmosphere.top_radius);
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
		
		float2 uv = TransmittanceLutCoordinatesToUv(atmosphere, coordinates);
		float3 transmittance_to_light = transmittance_lut.SampleLevel(sampler_linear_clamp, uv, 0).xyz;
		
		float3 slice_scattering = transmittance_to_light * medium.scattering * (1.0 / (4.0 * PI));
		result.multiple_scattering += ComputeScatteringIntegral(medium.scattering, slice_transmittance, medium.extinction) * transmittance;
		result.scattered_radiance  += ComputeScatteringIntegral(slice_scattering,  slice_transmittance, medium.extinction) * transmittance;
		
		transmittance *= slice_transmittance;
	}
	
#if 1
	if (t_max == t_bottom && t_bottom > 0.0) {
		// Account for light bounced off the ground.
		float3 position = planet_space_position + t_bottom * planet_space_direction;
		
		TransmittanceLutCoordinates coordinates;
		coordinates.view_height = length(position);
		float3 up_vector = position / coordinates.view_height;
		coordinates.cos_view_zenith_angle = dot(planet_space_sun_direction, up_vector);
		
		float2 uv = TransmittanceLutCoordinatesToUv(atmosphere, coordinates);
		float3 transmittance_to_light = transmittance_lut.SampleLevel(sampler_linear_clamp, uv, 0).xyz;
		
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
	float view_height = default_atmosphere.bottom_radius + saturate(group_uv.y + planet_radius_offset) * (default_atmosphere.top_radius - default_atmosphere.bottom_radius - planet_radius_offset);
	
	float2 rand = (MortonDecode(thread_index) + 0.5) * rcp(sqrt_sample_count);
	float theta = 2.0 * PI * rand.x;
	float phi   = acos(rand.y * 2.0 - 1.0);
	
	float sin_phi   = sin(phi);
	float cos_phi   = cos(phi);
	float sin_theta = sin(theta);
	float cos_theta = cos(theta);
	
	float3 planet_space_position  = float3(0.0, 0.0, view_height);
	float3 planet_space_direction = float3(cos_theta * sin_phi, sin_theta * sin_phi, cos_phi);
	
	MultipleScatteringValues result = IntegrateMultipleScattering(default_atmosphere, planet_space_position, planet_space_direction, planet_space_sun_direction);
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
	float t_bottom = RaySphereIntersect(planet_space_position, planet_space_direction, atmosphere.bottom_radius);
	float t_top = RaySphereIntersect(planet_space_position, planet_space_direction, atmosphere.top_radius);
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
		
		float2 uv = TransmittanceLutCoordinatesToUv(atmosphere, coordinates);
		float3 transmittance_to_light = transmittance_lut.SampleLevel(sampler_linear_clamp, uv, 0).xyz;
		
		float3 multiple_scattering = SampleMultipleScatteringLut(atmosphere, position, coordinates.cos_view_zenith_angle);
		
		float3 phase_times_scattering = medium.scattering_mie * mie_phase_value + medium.scattering_rayleigh * rayleigh_phase_value;
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
	
	float3 world_space_camera_position = float3(0.0, 0.0, 0.0);
	float3 planet_space_position = world_space_camera_position + float3(0, 0, default_atmosphere.bottom_radius + 0.1);
	float  view_height = length(planet_space_position);
	
	SkyPanoramaLutCoordinates coordinates = UvToSkyPanoramaLutCoordinates(default_atmosphere, view_height, thread_uv);
	
	float3 world_space_sun_direction = normalize(float3(1.0, 0.0, 0.2));
	
	float3 up_vector = planet_space_position / view_height;
	float cos_sun_zenith_angle = dot(up_vector, world_space_sun_direction);
	float3 planet_space_sun_direction = normalize(float3(sqrt(1.0 - cos_sun_zenith_angle * cos_sun_zenith_angle), 0.0, cos_sun_zenith_angle));
	
	planet_space_position = float3(0.0, 0.0, view_height);
	
	float sin_view_zenith_angle = sqrt(1.0 - coordinates.cos_view_zenith_angle * coordinates.cos_view_zenith_angle);
	
	float3 planet_space_direction;
	planet_space_direction.x = sin_view_zenith_angle * coordinates.cos_light_view_angle;
	planet_space_direction.y = sin_view_zenith_angle * sqrt(1.0 - coordinates.cos_light_view_angle * coordinates.cos_light_view_angle);
	planet_space_direction.z = coordinates.cos_view_zenith_angle;
	
	float3 scattering = IntegrateScattering(default_atmosphere, planet_space_position, planet_space_direction, planet_space_sun_direction);
	
	sky_panorama_lut[thread_id] = float4(scattering * 5.0, 1.0);
}
#endif // defined(SKY_PANORAMA_LUT)


#if defined(ATMOSPHERE_COMPOSITE)
[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	float3 planet_space_direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	
	float3 world_space_camera_position = float3(0.0, 0.0, 0.0) + view_space_ray.origin;
	float3 planet_space_position = world_space_camera_position + float3(0, 0, default_atmosphere.bottom_radius + 0.1);
	float  view_height = length(planet_space_position);
	
	float3 up_vector = normalize(planet_space_position);
	
	float3 world_space_sun_direction = normalize(float3(1.0, 0.0, 0.2));
	
	float3 side_vector    = normalize(cross(up_vector, planet_space_direction)); // Assumes non parallel vectors.
	float3 forward_vector = normalize(cross(side_vector, up_vector)); // Aligns toward the sun light but perpendicular to up vector.
	float2 light_on_plane = normalize(float2(dot(world_space_sun_direction, forward_vector), dot(world_space_sun_direction, side_vector)));
	
	SkyPanoramaLutCoordinates coordinates;
	coordinates.cos_view_zenith_angle = dot(planet_space_direction, up_vector);
	coordinates.cos_light_view_angle  = light_on_plane.x;
	
	bool intersect_ground = RaySphereIntersect(planet_space_position, planet_space_direction, default_atmosphere.bottom_radius) >= 0.0;
	
	float2 uv = SkyPanoramaLutCoordinatesToUv(default_atmosphere, intersect_ground, coordinates, view_height);
	float4 sky_radiance = sky_panorama_lut.SampleLevel(sampler_linear_clamp, uv, 0);
	
	scene_radiance[thread_id] = sky_radiance;
}
#endif // defined(ATMOSPHERE_COMPOSITE)

