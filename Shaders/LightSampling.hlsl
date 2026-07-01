#ifndef LIGHTSAMPLING_HLSL
#define LIGHTSAMPLING_HLSL

#include "AtmosphereSampling.hlsl"
#include "LightGridSampling.hlsl"
#include "BasicHashTable.hlsl"

struct VisibilityHashTableKey : HashTableKey {
	float cell_size;
};

VisibilityHashTableKey BuildVisibilityHashTableKey(float3 shading_position, u32 light_entity_index, float3 world_space_camera_position, float3 world_space_normal, float2 random_offset_2d) {
	compile_const float min_hash_cell_size     = 1.0 / 128.0;
	compile_const float inv_min_hash_cell_size = 1.0 / min_hash_cell_size;
	
	bool apply_random_offset = true;
	if (apply_random_offset) {
		float center_distance_to_camera = length(shading_position - world_space_camera_position);
		float center_cell_size = max(center_distance_to_camera * scene.visibility_hash_table_distance_to_cell_size_scale, min_hash_cell_size);
		
		float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
		float3x3 tangent_to_world = transpose(world_to_tangent);
		
		shading_position += mul(tangent_to_world, float3(random_offset_2d * center_cell_size, 0.0));
	}
	
	float distance_to_camera = length(shading_position - world_space_camera_position);
	HashCellSize cell_size = QuantizeHashCellSize(distance_to_camera * scene.visibility_hash_table_distance_to_cell_size_scale, min_hash_cell_size, inv_min_hash_cell_size);
	
	uint3 cell_position = ((s32x3)round(shading_position / cell_size.hash_cell_size)) & 0x1FFF;
	
	VisibilityHashTableKey result;
	result.key  = (u64)cell_position.x | ((u64)cell_position.y << 13) | ((u64)cell_position.z << 26) | ((u64)(cell_size.level_of_detail + 1) << 39) | ((u64)light_entity_index << 48);
	result.hash = WyHash32((u32)result.key, (u32)(result.key >> 32));
	result.cell_size = cell_size.hash_cell_size;
	
	return result;
}

struct RadianceHashTableKey : HashTableKey {
	
};

RadianceHashTableKey BuildRadianceHashTableKey(float3 shading_position, float3 world_space_camera_position, float3 world_space_normal) {
	compile_const float min_hash_cell_size     = 1.0 / 128.0;
	compile_const float inv_min_hash_cell_size = 1.0 / min_hash_cell_size;
	
	float distance_to_camera = length(shading_position - world_space_camera_position);
	HashCellSize cell_size = QuantizeHashCellSize(distance_to_camera * scene.radiance_hash_table_distance_to_cell_size_scale, min_hash_cell_size, inv_min_hash_cell_size);
	
	uint3 cell_position = ((s32x3)round(shading_position / cell_size.hash_cell_size)) & 0x1FFF;
	uint3 cell_normal   = select(world_space_normal >= 0.0, 1u, 0u);
	
	uint2 key;
	key.x = (cell_position.x & 0xFFFFu) | (cell_position.y                 << 16u);
	key.y = (cell_position.z & 0xFFFFu) | ((cell_size.level_of_detail + 1) << 16u) | (cell_normal.x << 29) | (cell_normal.y << 30) | (cell_normal.z << 31);
	
	RadianceHashTableKey result;
	result.key  = (u64)key.x | ((u64)key.y << 32);
	result.hash = WyHash32(key.x, key.y);
	
	return result;
}


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
	bool light_is_maybe_visible;
};

LightSample SampleLightUniform(float3 shading_position, inout uint hash) {
	bool has_global_light = scene.global_light_entity_index != u32_max;
	u32 light_count = has_global_light ? 1u : 0u;
	
	LightGridCellCoordinates cell_coordinates = ComputeLightGridCellCoordinates(shading_position);
	if (cell_coordinates.cell_offset != u32_max) {
		light_count += min(light_culling_grid[cell_coordinates.cell_offset], LightCullingConstants::max_lights_per_cell);
	}
	
	LightSample sample;
	sample.light_entity_index = u32_max;
	sample.inv_pdf = (float)light_count;
	sample.light_is_maybe_visible = false;
	
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

LightSample SampleLightWRS(float3 shading_position, float3 world_space_normal, float random, float minimum_light_weight = 0.0) {
	bool has_global_light = scene.global_light_entity_index != u32_max;
	u32 light_count = has_global_light ? 1u : 0u;
	
	LightGridCellCoordinates cell_coordinates = ComputeLightGridCellCoordinates(shading_position);
	if (cell_coordinates.cell_offset != u32_max) {
		light_count += min(light_culling_grid[cell_coordinates.cell_offset], LightCullingConstants::max_lights_per_cell);
	}
	
	LightSample sample;
	sample.light_entity_index = u32_max;
	sample.inv_pdf            = 0.0;
	sample.light_is_maybe_visible = false;
	
	if (light_count != 0) {
		float weight_sum = 0.0;
		for (uint light_index_in_cell = 0; light_index_in_cell < light_count; light_index_in_cell += 1) {
			u32 light_entity_index = 0;
			if (has_global_light && light_index_in_cell == 0) {
				light_entity_index = scene.global_light_entity_index;
			} else {
				light_entity_index = light_culling_grid[cell_coordinates.cell_offset + light_index_in_cell + (has_global_light ? 0u : 1u)];
			}
			
			// TODO: Account for BRDF during sampling.
			LightShadingInfo shading_info = ComputeLightShadingInfo(shading_position, light_entity_index);
			float weight = dot(shading_info.light_irradiance, rec709_luminance_coefficients) * saturate(dot(shading_info.light_direction, world_space_normal));
			
			weight = log2(weight + 1.0);
			
			if (weight > 0) {
				weight_sum += weight;
				
				float p = (weight / weight_sum);
				bool commit_sample = (random < p);
				
				if (commit_sample) {
					sample.light_entity_index = light_entity_index;
					sample.inv_pdf            = (1.0 / weight);
				}
				
				// Warp the random number instead of generating a new one.
				random = commit_sample ? (random / p) : ((random - p) / (1.0 - p));
			}
		}
		sample.inv_pdf *= weight_sum;
	}
	
	return sample;
}

LightSample SampleLightWRS(
	float3 shading_position,
	float3 world_space_normal,
	float random,
	float minimum_light_weight,
	bool src_tile_valid,
	u32 src_tile_index,
	RWStructuredBuffer<u32> visible_light_tile_list
) {
	bool has_global_light = scene.global_light_entity_index != u32_max;
	u32 light_count = has_global_light ? 1u : 0u;
	
	LightGridCellCoordinates cell_coordinates = ComputeLightGridCellCoordinates(shading_position);
	if (cell_coordinates.cell_offset != u32_max) {
		light_count += min(light_culling_grid[cell_coordinates.cell_offset], LightCullingConstants::max_lights_per_cell);
	}
	
	LightSample sample;
	sample.light_entity_index = u32_max;
	sample.inv_pdf            = 0.0;
	sample.light_is_maybe_visible = false;
	
	if (light_count != 0) {
		u32 src_light_index_in_tile = 0;
		uint visible_light_entity_index = src_tile_valid ? visible_light_tile_list[src_tile_index * LightingConstants::visible_light_tile_area + src_light_index_in_tile] : u32_max;
		src_light_index_in_tile += 1;
		
		float weight_sum = 0.0;
		for (uint light_index_in_cell = 0; light_index_in_cell < light_count; light_index_in_cell += 1) {
			u32 light_entity_index = 0;
			if (has_global_light && light_index_in_cell == 0) {
				light_entity_index = scene.global_light_entity_index;
			} else {
				light_entity_index = light_culling_grid[cell_coordinates.cell_offset + light_index_in_cell + (has_global_light ? 0u : 1u)];
			}
			
			// TODO: Account for BRDF during sampling.
			LightShadingInfo shading_info = ComputeLightShadingInfo(shading_position, light_entity_index);
			float weight = dot(shading_info.light_irradiance, rec709_luminance_coefficients) * saturate(dot(shading_info.light_direction, world_space_normal));
			
			weight = log2(weight + 1.0);
			
			while (src_tile_valid && (visible_light_entity_index < light_entity_index) && (src_light_index_in_tile < LightingConstants::visible_light_tile_area)) {
				visible_light_entity_index = visible_light_tile_list[src_tile_index * LightingConstants::visible_light_tile_area + src_light_index_in_tile];
				src_light_index_in_tile += 1;
			}
			
			bool light_is_maybe_visible = (visible_light_entity_index == light_entity_index);
			weight *= light_is_maybe_visible ? 32.0 : 1.0;
			
			if (weight > minimum_light_weight) {
				weight_sum += weight;
				
				float p = (weight / weight_sum);
				bool commit_sample = (random < p);
				
				if (commit_sample) {
					sample.light_entity_index = light_entity_index;
					sample.inv_pdf            = (1.0 / weight);
					sample.light_is_maybe_visible = light_is_maybe_visible;
				}
				
				// Warp the random number instead of generating a new one.
				random = commit_sample ? (random / p) : ((random - p) / (1.0 - p));
			}
		}
		sample.inv_pdf *= weight_sum;
	}
	
	return sample;
}


compile_const float light_penumbra_size = 0.00459903;
// compile_const float light_penumbra_size = 0.01;
// compile_const float light_penumbra_size = 0.1;

struct ShadowSampler {
	float2 penumbra_noise;
	
	float EvaluateVisibility(float3 ray_origin, float3 ray_direction, float ray_length) {
		RayQuery<
			RAY_FLAG_CULL_NON_OPAQUE |
			RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
			RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
			RAY_FLAG_NONE
		> ray_query;
		
		float3x3 world_to_light = BuildOrthonormalBasis(ray_direction);
		float3x3 light_to_world = transpose(world_to_light);
		
		RayDesc ray_desc;
		ray_desc.Origin    = ray_origin;
		ray_desc.Direction = normalize(ray_direction + mul(light_to_world, float3(penumbra_noise * light_penumbra_size, 0.0)));
		ray_desc.TMin      = 0.0;
		ray_desc.TMax      = ray_length;
		
		ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
		
		while (ray_query.Proceed()) {
			
		}
		
		return ray_query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
	}
};

#endif // LIGHTSAMPLING_HLSL
