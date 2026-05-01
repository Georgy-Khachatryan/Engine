#ifndef ATMOSPHERESAMPLING_HLSL
#define ATMOSPHERESAMPLING_HLSL

//
// Sebastien Hillaire. 2020. A Scalable and Production Ready Sky and Atmosphere Rendering Technique.
// https://github.com/sebh/UnrealEngineSkyAtmosphere see license in THIRD_PARTY_LICENSES.md
//

compile_const float2 inv_transmittance_lut_size       = 1.0 / AtmosphereParameters::transmittance_lut_size;
compile_const float2 inv_multiple_scattering_lut_size = 1.0 / AtmosphereParameters::multiple_scattering_lut_size;
compile_const float2 inv_sky_panorama_lut_size        = 1.0 / AtmosphereParameters::sky_panorama_lut_size;
compile_const float  planet_radius_offset             = 0.01;
compile_const uint   thread_group_size                = AtmosphereParameters::thread_group_size;
compile_const float  world_to_planet_space_scale      = 1.0 / 1000.0;

float3 TransformWorldToPlanetSpace(float3 world_space_position, float bottom_radius) {
	float3 planet_space_position = world_space_position * world_to_planet_space_scale;
	return float3(planet_space_position.xy, max(planet_space_position.z + 0.05, 0.0) + bottom_radius + 0.05);
}

float RayAtmosphereIntersect(float3 ray_origin, float3 ray_direction, float radius) {
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
	float H   = sqrt(max(Pow2(atmosphere.top_radius)   - Pow2(atmosphere.bottom_radius), 0.0));
	float rho = sqrt(max(Pow2(coordinates.view_height) - Pow2(atmosphere.bottom_radius), 0.0));
	
	float discriminant = Pow2(coordinates.view_height) * (Pow2(coordinates.cos_view_zenith_angle) - 1.0) + Pow2(atmosphere.top_radius);
	float d = max(sqrt(discriminant) - coordinates.view_height * coordinates.cos_view_zenith_angle, 0.0); // Distance to atmosphere boundary.
	
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
	float vhorizon = sqrt(Pow2(view_height) - Pow2(atmosphere.bottom_radius));
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
	float vhorizon = sqrt(Pow2(view_height) - Pow2(atmosphere.bottom_radius));
	float cos_beta = vhorizon / view_height;
	float beta     = acos(clamp(cos_beta, -1.0, 1.0));
	float zenith_horizon_angle = PI - beta;
	
	float2 uv;
	if (intersect_ground == false) {
		float coord = 1.0 - sqrt(max(1.0 - (acos(clamp(coordinates.cos_view_zenith_angle, -1.0, 1.0)) / zenith_horizon_angle), 0.0));
		uv.y = coord * 0.5;
	} else {
		float coord = sqrt(max((acos(clamp(coordinates.cos_view_zenith_angle, -1.0, 1.0)) - zenith_horizon_angle) / beta, 0.0));
		uv.y = coord * 0.5 + 0.5;
	}
	uv.x = sqrt(max(0.5 - coordinates.cos_light_view_angle * 0.5, 0.0));
	
	return uv;
}

#endif // ATMOSPHERESAMPLING_HLSL
