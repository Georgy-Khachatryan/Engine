#include "Basic.hlsl"

using IndirectArgumentsLayout = CloudCullingIndirectArgumentsLayout;

u32 EncodeCloudCullingCellIndex(uint3 cell_coordinates) {
	return cell_coordinates.x | (cell_coordinates.y << 6) | (cell_coordinates.z << 12);
}

uint3 DecodeCloudCullingCellIndex(u32 index) {
	return uint3(index, index >> 6, index >> 12) & uint3(0x3F, 0x3F, 0x7);
}

#if defined(CLOUD_ENTITY_CULLING)
compile_const uint thread_group_size = CloudCullingConstants::thread_group_size;

void AppendCloudCullingCommand(uint cloud_entity_index, uint aabb_volume_offset, uint packed_aabb, uint bin_index) {
	uint command_index = 0;
	InterlockedAdd(indirect_arguments[bin_index].w, 1u, command_index);
	
	if (command_index < CloudCullingConstants::culling_command_bin_size) {
		uint2 culling_command = uint2(cloud_entity_index | (aabb_volume_offset << 17), packed_aabb);
		
		uint bin_base_offset = bin_index * CloudCullingConstants::culling_command_bin_size;
		cloud_culling_commands[bin_base_offset + command_index] = culling_command;
		
		InterlockedMax(indirect_arguments[bin_index + IndirectArgumentsLayout::CloudCullingCommands].x, DivideAndRoundUp((command_index + 1) << bin_index, thread_group_size));
	}
}

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint cloud_entity_index = thread_id;
	if (BitArrayTestBit(cloud_volume_alive_mask, cloud_entity_index) == false) return;
	
	GpuCloudVolumeEntityData cloud = cloud_volume_data[cloud_entity_index];
	if (cloud.sdf_texture == u32_max) return;
	
	float3 aabb_radius = mul(abs(QuatToRotationMatrix(Conjugate(cloud.world_to_model_rotation))), cloud.world_space_size * 0.5);
	float3 aabb_min = cloud.world_space_position - aabb_radius;
	float3 aabb_max = cloud.world_space_position + aabb_radius;
	
	compile_const float3 grid_size_cells = CloudCullingConstants::grid_size_cells;
	float grid_cell_size = scene.clouds.world_space_size.x / grid_size_cells.x;
	
	uint3 aabb_min_cells = (uint3)clamp(floor((aabb_min - scene.clouds.world_space_position) / grid_cell_size), 0.0, grid_size_cells);
	uint3 aabb_max_cells = (uint3)clamp(ceil((aabb_max  - scene.clouds.world_space_position) / grid_cell_size), 0.0, grid_size_cells);
	uint3 aabb_size_cells = aabb_max_cells - aabb_min_cells;
	uint aabb_volume_cells = aabb_size_cells.x * aabb_size_cells.y * aabb_size_cells.z;
	
	uint packed_aabb_min = EncodeCloudCullingCellIndex(aabb_min_cells - 0);
	uint packed_aabb_max = EncodeCloudCullingCellIndex(aabb_max_cells - 1);
	uint packed_aabb     = packed_aabb_min | (packed_aabb_max << 15);
	
	if (aabb_volume_cells != 0) {
		float3 target_resolution = aabb_radius * 2.0 * scene.clouds.inv_world_space_size * CloudConstants::cloud_volume_size;
		
		u32 max_target_resolution = (u32)max(max(target_resolution.x, target_resolution.y), max(target_resolution.z, 1.0));
		InterlockedMax(texture_streaming_feedback[cloud.sdf_texture], max_target_resolution);
	}
	
	uint aabb_volume_offset = 0;
	while (aabb_volume_cells != 0) {
		uint bin_index = firstbitlow(aabb_volume_cells);
		AppendCloudCullingCommand(cloud_entity_index, aabb_volume_offset, packed_aabb, bin_index);
		
		aabb_volume_cells  -= (1u << bin_index);
		aabb_volume_offset += (1u << bin_index);
	}
}
#endif // defined(CLOUD_ENTITY_CULLING)

#if defined(CLOUD_CULLING)
compile_const uint thread_group_size = CloudCullingConstants::thread_group_size;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if ((thread_id >> constants.bin_index) >= indirect_arguments[constants.bin_index].w) return;
	
	uint bin_base_offset = constants.bin_index * CloudCullingConstants::culling_command_bin_size;
	uint2 cloud_culling_command = cloud_culling_commands[bin_base_offset + (thread_id >> constants.bin_index)];
	
	uint cloud_entity_index = (cloud_culling_command.x & 0x1FFFFu);
	uint aabb_volume_offset = (cloud_culling_command.x >> 17) + (thread_id & CreateBitMaskSmall(constants.bin_index));
	uint3 aabb_min_cells    = DecodeCloudCullingCellIndex(cloud_culling_command.y >>  0) + 0;
	uint3 aabb_max_cells    = DecodeCloudCullingCellIndex(cloud_culling_command.y >> 15) + 1;
	uint3 aabb_size_cells   = aabb_max_cells - aabb_min_cells;
	
	uint3 cell_coordinates;
	cell_coordinates.z = (aabb_volume_offset / (aabb_size_cells.x * aabb_size_cells.y));
	cell_coordinates.y = (aabb_volume_offset % (aabb_size_cells.x * aabb_size_cells.y)) / aabb_size_cells.x;
	cell_coordinates.x = (aabb_volume_offset % (aabb_size_cells.x * aabb_size_cells.y)) % aabb_size_cells.x;
	cell_coordinates += aabb_min_cells;
	
	compile_const float3 grid_size_cells = CloudCullingConstants::grid_size_cells;
	float grid_cell_size   = scene.clouds.world_space_size.x / grid_size_cells.x;
	float grid_cell_radius = grid_cell_size * SQRT_THREE_OVER_TWO;
	
	float3 cell_center_position = (float3)(cell_coordinates + 0.5) * grid_cell_size + scene.clouds.world_space_position;
	
	GpuCloudVolumeEntityData cloud = cloud_volume_data[cloud_entity_index];
	
	float3x3 world_to_model_rotation = QuatToRotationMatrix(cloud.world_to_model_rotation);
	float3 model_space_position = mul(world_to_model_rotation, cell_center_position - cloud.world_space_position);
	
	float distance = Length2(clamp(model_space_position, cloud.world_space_size * -0.5, cloud.world_space_size * +0.5) - model_space_position);
	bool is_visible = distance < Pow2(grid_cell_radius);
	
	// TODO: We could improve culling efficiency by sampling SDF texture and checking the distance against cell radius.
	// This would require relatively large maximum distance to be stored in the SDF, or even using a separate SDF texture.
	
	if (is_visible) {
		uint cell_index  = EncodeCloudCullingCellIndex(cell_coordinates);
		uint cell_offset = cell_index * CloudCullingConstants::max_elements_per_cell;
		
		BitArraySetBit(cloud_culling_grid, cloud_entity_index, cell_offset);
	}
}
#endif // defined(CLOUD_CULLING)

#if defined(BUILD_CLOUD_UPDATE_LIST)
[ThreadGroupSize(4, 4, 4)]
void MainCS(uint3 thread_id : SV_DispatchThreadID, uint thread_index : SV_GroupIndex) {
	uint3 cell_coordinates = thread_id;
	
	uint cell_index  = EncodeCloudCullingCellIndex(cell_coordinates);
	uint cell_offset = cell_index * CloudCullingConstants::max_elements_per_cell;
	
	// Update any cells that have cloud volumes.
	bool is_occupied = false;
	for (uint i = 0; i < CloudCullingConstants::max_elements_per_cell; i += 1) {
		is_occupied |= cloud_culling_grid[cell_offset + i] != 0;
	}
	
	// Update any cells that had clouds last frame to clean up stale data left behind by moving clouds.
	if (is_occupied == false) {
		u64 voxel = sdf_cloud_volume_mask[thread_id / 2];
		uint voxel_index = RowMajorEncode3D(thread_id * 2, 2);
		
		// Culling happens at 64x64x16 virtual resolution, while voxels are stored at 128x128x32 resolution and
		// packed into 32x32x8 blocks, 4x4x4 each. So we need to check 2x2x2 texel block for each culling thread.
		is_occupied |= ((voxel >> voxel_index) & 0x00330033) != 0;
	}
	
	if (is_occupied) {
		uint command_index = 0;
		InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::CoarseCloudUpdateList].x, 8u, command_index);
		
		cloud_update_list[command_index / 8u] = cell_index;
	}
}
#endif // defined(BUILD_CLOUD_UPDATE_LIST)

#if defined(COMPOSITE_CLOUD_VOLUME)
groupshared uint gs_is_occupied;

[ThreadGroupSize(64, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	if (thread_index == 0) {
		gs_is_occupied = 0;
	}
	
	uint cell_index = cloud_update_list[group_id >> 3];
	uint3 thread_id = DecodeCloudCullingCellIndex(cell_index) * 8 + RowMajorDecode3D(group_id, 1) * 4 + RowMajorDecode3D(thread_index, 2);
	
	float3 thread_uv = (thread_id + 0.5) / CloudConstants::cloud_volume_size;
	float3 world_space_position = thread_uv * scene.clouds.world_space_size + scene.clouds.world_space_position;
	uint cell_offset = cell_index * CloudCullingConstants::max_elements_per_cell;
	
	float dimensional_profile_composite = 0.0;
	for (uint i = 0; i < CloudCullingConstants::max_elements_per_cell; i += 1) {
		uint mask = cloud_culling_grid[cell_offset + i];
		
		while (mask != 0) {
			uint cloud_volume_index = i * 32u + firstbitlow(mask);
			mask &= (mask - 1);
			
			GpuCloudVolumeEntityData cloud = cloud_volume_data[cloud_volume_index];
			
			float3 sample_uvw = QuatMul(cloud.world_to_model_rotation, (world_space_position - cloud.world_space_position)) / cloud.world_space_size + 0.5;
			if (any(sample_uvw <= 0.0) || any(sample_uvw >= 1.0)) continue;
			
			Texture3D<float> sdf_grid = ResourceDescriptorHeap[cloud.sdf_texture];
			float dimensional_profile = sdf_grid.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
			
			dimensional_profile_composite = max(dimensional_profile_composite, dimensional_profile);
		}
	}
	
	// TODO: BC4 compress the output.
	sdf_cloud_volume[thread_id] = dimensional_profile_composite;
	
	GroupMemoryBarrierWithGroupSync();
	
	bool wave_is_occlupied = WaveActiveAnyTrue(dimensional_profile_composite > (0.5 / 255.0));
	if (WaveIsFirstLane() && wave_is_occlupied) {
		InterlockedOr(gs_is_occupied, 1u);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		sdf_cloud_volume_transient_mask[thread_id / 4u] = gs_is_occupied;
	}
}
#endif // defined(COMPOSITE_CLOUD_VOLUME)

#if defined(BUILD_CLOUD_VOLUME_MASK)
groupshared u64 gs_occupancy_mask;

[ThreadGroupSize(4, 4, 4)]
void MainCS(uint3 thread_id : SV_DispatchThreadID, uint thread_index : SV_GroupIndex) {
	if (thread_index == 0) {
		gs_occupancy_mask = 0;
	}
	
	bool is_occupied = sdf_cloud_volume_transient_mask[thread_id] != 0;
	u64 lane_occupancy_mask = is_occupied ? ((u64)1 << thread_index) : 0;
	u64 wave_occupancy_mask = WaveActiveBitOr(lane_occupancy_mask);
	
	GroupMemoryBarrierWithGroupSync();
	
	if (WaveIsFirstLane() && wave_occupancy_mask) {
		InterlockedOr(gs_occupancy_mask, wave_occupancy_mask);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		sdf_cloud_volume_mask[thread_id / 4u] = gs_occupancy_mask;
	}
	
	// Fine cloud update passes run at half volume resolution, so they need 1 4x4x4 thread group for each 8x8x8 full resolution block.
	bool is_octant_occupied = (thread_index & 0b101010) == thread_index && ((gs_occupancy_mask >> thread_index) & 0x00330033) != 0;
	if (is_octant_occupied) {
		uint command_index = 0;
		InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::FineCloudUpdateList].x, 1u, command_index);
		
		cloud_update_list[command_index] = RowMajorEncode3D(thread_id / 2, 8);
	}
}
#endif // defined(BUILD_CLOUD_VOLUME_MASK)
