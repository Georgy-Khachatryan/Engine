#include "Basic.hlsl"

compile_const uint thread_group_size = LightCullingConstants::thread_group_size;

#if defined(LIGHT_ENTITY_CULLING)
void AppendLightCullingCommand(uint light_entity_index, uint aabb_volume_offset, uint packed_aabb, uint bin_index) {
	uint command_index = 0;
	InterlockedAdd(indirect_arguments[bin_index].w, 1u, command_index);
	
	if (command_index < LightCullingConstants::culling_command_bin_size) {
		uint2 culling_command = uint2(light_entity_index | ((aabb_volume_offset >> 8) << 28), packed_aabb | (aabb_volume_offset << 24));
		
		uint bin_base_offset = bin_index * LightCullingConstants::culling_command_bin_size;
		light_culling_commands[bin_base_offset + command_index] = culling_command;
		
		InterlockedMax(indirect_arguments[bin_index].x, DivideAndRoundUp((command_index + 1) << bin_index, thread_group_size));
	}
}

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint light_entity_index = thread_id;
	if (BitArrayTestBit(light_alive_mask, light_entity_index) == false) return;
	
	GpuLightEntityData light = light_entity_data[light_entity_index];
	if (light.type == LightType::Global) return;
	
	float3 aabb_min = 0.0;
	float3 aabb_max = 0.0;
	
	if (light.type == LightType::Spot || light.type == LightType::Point) {
		aabb_min = light.light_position - light.attenuation_radius;
		aabb_max = light.light_position + light.attenuation_radius;
	}
	
	if (light.type == LightType::Spot) {
		// Spot light AABB is computed as an a combined AABBs of:
		// - Disk cap: Based on this article https://iquilezles.org/articles/ellipses/
		// - Cone apex vertex.
		float3 cap_position = light.light_position - light.light_direction * light.attenuation_radius;
		float  cap_radius   = light.tan_attenuation_angle * light.attenuation_radius;
		float3 cap_extent   = sqrt(1.0 - Pow2(light.light_direction)) * cap_radius;
		
		aabb_min = max(aabb_min, min(light.light_position, cap_position - cap_extent));
		aabb_max = min(aabb_max, max(light.light_position, cap_position + cap_extent));
	}
	
	compile_const float grid_cell_size  = LightCullingConstants::grid_cell_size;
	compile_const float grid_size_cells = LightCullingConstants::grid_size_cells;
	
	float3 origin = round(scene.world_space_camera_position / grid_cell_size) * grid_cell_size;
	uint3 aabb_min_cells = (uint3)clamp(floor(((aabb_min - origin) / grid_cell_size) + grid_size_cells * 0.5), 0.0, grid_size_cells);
	uint3 aabb_max_cells = (uint3)clamp(ceil(((aabb_max - origin) / grid_cell_size) + grid_size_cells * 0.5), 0.0, grid_size_cells);
	uint3 aabb_size_cells = aabb_max_cells - aabb_min_cells;
	uint aabb_volume_cells = aabb_size_cells.x * aabb_size_cells.y * aabb_size_cells.z;
	
	uint packed_aabb_min = (aabb_min_cells.x - 0) | ((aabb_min_cells.y - 0) << 4) | ((aabb_min_cells.z - 0) << 8);
	uint packed_aabb_max = (aabb_max_cells.x - 1) | ((aabb_max_cells.y - 1) << 4) | ((aabb_max_cells.z - 1) << 8);
	uint packed_aabb     = packed_aabb_min | (packed_aabb_max << 12);
	
	uint aabb_volume_offset = 0;
	while (aabb_volume_cells != 0) {
		uint bin_index = firstbitlow(aabb_volume_cells);
		AppendLightCullingCommand(light_entity_index, aabb_volume_offset, packed_aabb, bin_index);
		
		aabb_volume_cells  -= (1u << bin_index);
		aabb_volume_offset += (1u << bin_index);
	}
}
#endif // defined(LIGHT_ENTITY_CULLING)


#if defined(LIGHT_CULLING)
[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if ((thread_id >> constants.bin_index) >= indirect_arguments[constants.bin_index].w) return;
	
	uint bin_base_offset = constants.bin_index * LightCullingConstants::culling_command_bin_size;
	uint2 light_culling_command = light_culling_commands[bin_base_offset + (thread_id >> constants.bin_index)];
	
	uint light_entity_index = (light_culling_command.x & 0xFFFFFFFu);
	uint aabb_volume_offset = (((light_culling_command.x >> 28) << 8) | (light_culling_command.y >> 24)) + (thread_id & CreateBitMaskSmall(constants.bin_index));
	uint3 aabb_min_cells    = (uint3(light_culling_command.y >> 0,  light_culling_command.y >> 4,  light_culling_command.y >> 8)  & 0xF) + 0;
	uint3 aabb_max_cells    = (uint3(light_culling_command.y >> 12, light_culling_command.y >> 16, light_culling_command.y >> 20) & 0xF) + 1;
	uint3 aabb_size_cells   = aabb_max_cells - aabb_min_cells;
	
	uint3 cell_coordinates;
	cell_coordinates.z = (aabb_volume_offset / (aabb_size_cells.x * aabb_size_cells.y));
	cell_coordinates.y = (aabb_volume_offset % (aabb_size_cells.x * aabb_size_cells.y)) / aabb_size_cells.x;
	cell_coordinates.x = (aabb_volume_offset % (aabb_size_cells.x * aabb_size_cells.y)) % aabb_size_cells.x;
	cell_coordinates += aabb_min_cells;
	
	compile_const float grid_cell_size   = LightCullingConstants::grid_cell_size;
	compile_const float grid_size_cells  = LightCullingConstants::grid_size_cells;
	compile_const float grid_cell_radius = grid_cell_size * SQRT3_OVER_TWO;
	
	float3 origin = round(scene.world_space_camera_position / grid_cell_size) * grid_cell_size;
	float3 cell_center_position = ((float3)cell_coordinates + 0.5 - grid_size_cells * 0.5) * grid_cell_size + origin;
	
	GpuLightEntityData light = light_entity_data[light_entity_index];
	
	bool is_visible = false;
	if (light.type == LightType::Spot || light.type == LightType::Point) {
		float3 closest_point = clamp(light.light_position, cell_center_position - grid_cell_size * 0.5, cell_center_position + grid_cell_size * 0.5);
		float3 light_to_cell = (closest_point - light.light_position);
		is_visible = Length2(light_to_cell) < Pow2(light.attenuation_radius);
	}
	
	if (is_visible && light.type == LightType::Spot) {
		float3 light_to_cell  = cell_center_position - light.light_position;
		float length_square   = Length2(light_to_cell);
		float axis_projection = dot(light_to_cell, -light.light_direction);
		
		float distance_to_cone = (sqrt(length_square - Pow2(axis_projection)) - axis_projection * light.tan_attenuation_angle) * light.cos_attenuation_angle;
		is_visible = (axis_projection >= -grid_cell_radius) && (distance_to_cone <= grid_cell_radius);
	}
	
	if (is_visible) {
		uint cell_index = cell_coordinates.x | (cell_coordinates.y << 4) | (cell_coordinates.z << 8);
		
		uint light_index_in_cell = 0;
		InterlockedAdd(light_culling_grid[cell_index * LightCullingConstants::max_elements_per_cell], 1u, light_index_in_cell);
		
		if (light_index_in_cell < LightCullingConstants::max_lights_per_cell) {
			light_culling_grid[cell_index * LightCullingConstants::max_elements_per_cell + light_index_in_cell + 1] = light_entity_index;
		}
	}
}
#endif // defined(LIGHT_CULLING)
