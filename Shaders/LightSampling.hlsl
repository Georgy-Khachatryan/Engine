#ifndef LIGHTSAMPLING_HLSL
#define LIGHTSAMPLING_HLSL

#include "AtmosphereSampling.hlsl"
#include "LightGridSampling.hlsl"

struct LightShadingInfo {
	float3 light_direction;
	float shadow_ray_length;
	float3 light_irradiance;
};

LightShadingInfo ComputeLightShadingInfo(float3 shading_position, u32 light_entity_index) {
	GpuLightEntityData light = light_entity_data[light_entity_index];
	
	LightShadingInfo shading_info;
	shading_info.light_irradiance = light.radiance_or_irradiance * light.color;
	
	if (light.type == LightType::Global) {
		shading_info.light_direction   = light.light_direction;
		shading_info.shadow_ray_length = 1024.0;
		shading_info.light_irradiance *= SampleTransmittanceLUT(atmosphere, transmittance_lut, shading_position);
	} else {
		float3 light_vector = light.light_position - shading_position;
		float distance_to_light_square = dot(light_vector, light_vector);
		float distance_to_light = sqrt(distance_to_light_square);
		
		shading_info.light_direction   = light_vector / distance_to_light;
		shading_info.shadow_ray_length = distance_to_light;
		
		// Converts Spot or Point light radiance to irradiance. Attenuation radius and light radius are not physically based.
		float attenuation = SmoothStep(light.distance_attenuation, distance_to_light) / (distance_to_light_square + Pow2(light.light_radius) * 0.5);
		
		if (light.type == LightType::Spot) {
			attenuation *= SmoothStep(light.angle_attenuation, dot(shading_info.light_direction, light.light_direction));
		}
		
		shading_info.light_irradiance *= attenuation;
	}
	
	return shading_info;
}


struct LightSample {
	u32 light_entity_index;
	float inv_pdf;
};

LightSample SampleLight(float3 shading_position, inout uint hash) {
	bool has_global_light = scene.global_light_entity_index != u32_max;
	u32 light_count = has_global_light ? 1u : 0u;
	
	LightGridCellCoordinates cell_coordinates = ComputeLightGridCellCoordinates(shading_position);
	if (cell_coordinates.cell_offset != u32_max) {
		light_count += min(light_culling_grid[cell_coordinates.cell_offset], LightCullingConstants::max_lights_per_cell);
	}
	
	LightSample sample;
	sample.light_entity_index = u32_max;
	sample.inv_pdf = (float)light_count;
	
	if (light_count != 0) {
		u32 light_index_in_cell = ComputeRandomU32(hash, light_count);
		
		if (has_global_light && light_index_in_cell == 0) {
			sample.light_entity_index = scene.global_light_entity_index;
		} else {
			sample.light_entity_index = light_culling_grid[cell_coordinates.cell_offset + light_index_in_cell + (has_global_light ? 0u : 1u)];
		}
	}
	
	return sample;
}

LightSample SampleLightWithBlueNoise(float3 shading_position, float blue_noise) {
	bool has_global_light = scene.global_light_entity_index != u32_max;
	u32 light_count = has_global_light ? 1u : 0u;
	
	LightGridCellCoordinates cell_coordinates = ComputeLightGridCellCoordinates(shading_position);
	if (cell_coordinates.cell_offset != u32_max) {
		light_count += min(light_culling_grid[cell_coordinates.cell_offset], LightCullingConstants::max_lights_per_cell);
	}
	
	LightSample sample;
	sample.light_entity_index = u32_max;
	sample.inv_pdf = (float)light_count;
	
	if (light_count != 0) {
		u32 light_index_in_cell = clamp((u32)(blue_noise * light_count), 0, light_count - 1);
		
		if (has_global_light && light_index_in_cell == 0) {
			sample.light_entity_index = scene.global_light_entity_index;
		} else {
			sample.light_entity_index = light_culling_grid[cell_coordinates.cell_offset + light_index_in_cell + (has_global_light ? 0u : 1u)];
		}
	}
	
	return sample;
}


float TraceShadowRay(float3 ray_origin, float3 ray_direction, float ray_length) {
	RayQuery<
		RAY_FLAG_CULL_NON_OPAQUE |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
		// RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_NONE
	> ray_query;
	
	RayDesc ray_desc;
	ray_desc.Origin    = ray_origin;
	ray_desc.Direction = ray_direction;
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = ray_length;
	
	ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
	
	while (ray_query.Proceed()) {
		
	}
	
	return ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

#endif // LIGHTSAMPLING_HLSL
