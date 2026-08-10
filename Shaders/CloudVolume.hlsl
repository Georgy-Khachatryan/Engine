#include "Basic.hlsl"

#if defined(COMPOSITE_CLOUD_VOLUME)
groupshared uint gs_is_occupied;

[ThreadGroupSize(4, 4, 4)]
void MainCS(uint3 thread_id : SV_DispatchThreadID, uint thread_index : SV_GroupIndex) {
	float3 thread_uv = (thread_id + 0.5) / CloudConstants::cloud_volume_size;
	
	if (thread_index == 0) {
		gs_is_occupied = 0;
	}
	
	float3 world_space_position = (thread_uv - 0.5) * scene.clouds.world_space_size + scene.clouds.world_space_position;
	
	// TODO: This is very slow, do a volume culling prepass and BC4 compress the output.
	uint dword_count = DivideAndRoundUp(constants.cloud_volume_count, 32u);
	
	float dimensional_profile_composite = 0.0;
	for (uint i = 0; i < dword_count; i += 1) {
		uint mask = cloud_volume_alive_mask[i];
		
		while (mask != 0) {
			uint cloud_volume_index = i * 32u + firstbitlow(mask);
			mask &= (mask - 1);
			
			GpuCloudVolumeEntityData volume = cloud_volume_data[cloud_volume_index];
			if (volume.sdf_texture == u32_max) continue;
			
			float3 sample_uvw = QuatMul(volume.world_to_model_rotation, (world_space_position - volume.world_space_position)) / volume.world_space_size + 0.5;
			if (any(sample_uvw <= 0.0) || any(sample_uvw >= 1.0)) continue;
			
			Texture3D<float> sdf_grid = ResourceDescriptorHeap[volume.sdf_texture];
			float dimensional_profile = sdf_grid.SampleLevel(sampler_linear_clamp, sample_uvw, 0);
			
			dimensional_profile_composite = max(dimensional_profile_composite, dimensional_profile);
		}
	}
	
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
}
#endif // defined(BUILD_CLOUD_VOLUME_MASK)
