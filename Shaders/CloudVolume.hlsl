#include "Basic.hlsl"

#if defined(COMPOSITE_CLOUD_VOLUME)
[ThreadGroupSize(4, 4, 4)]
void MainCS(uint3 thread_id : SV_DispatchThreadID) {
	float3 thread_uv = (thread_id + 0.5) / CloudConstants::cloud_volume_size;
	
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
}
#endif // defined(COMPOSITE_CLOUD_VOLUME)
