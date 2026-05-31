#ifndef LIGHTGRIDSAMPLING_HLSL
#define LIGHTGRIDSAMPLING_HLSL

struct LightGridCellCoordinates {
	u32 cascade_index;
	u32 cell_offset;
};

LightGridCellCoordinates ComputeLightGridCellCoordinates(float3 world_space_position) {
	LightGridCellCoordinates result;
	result.cascade_index = u32_max;
	result.cell_offset   = u32_max;
	
	for (u32 i = 0; (i < scene.light_grid_cascade_descs.count) && (result.cascade_index == u32_max); i += 1) {
		float4 cascade_desc   = scene.light_grid_cascade_descs[i];
		float3 cascade_offset = world_space_position - cascade_desc.xyz;
		if (all(cascade_offset >= 0.0) && all(cascade_offset < cascade_desc.w)) {
			result.cascade_index = i;
		}
	}
	
	if (result.cascade_index != u32_max) {
		float4 cascade_desc   = scene.light_grid_cascade_descs[result.cascade_index];
		float3 cascade_offset = world_space_position - cascade_desc.xyz;
		
		float grid_cell_size   = LightCullingConstants::grid_cell_size * (1u << result.cascade_index);
		uint3 cell_coordinates = (uint3)floor((world_space_position - cascade_desc.xyz) / grid_cell_size);
		
		uint cell_index = cell_coordinates.x | (cell_coordinates.y << 4) | (cell_coordinates.z << 8);
		result.cell_offset = cell_index * LightCullingConstants::max_elements_per_cell + result.cascade_index * LightCullingConstants::max_elements_per_cascade;
	}
	
	return result;
}

#endif // LIGHTGRIDSAMPLING_HLSL
