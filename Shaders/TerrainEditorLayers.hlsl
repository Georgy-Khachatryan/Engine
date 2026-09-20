#include "Basic.hlsl"
#include "Noise.hlsl"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * constants.inv_render_target_size;
	
	float2 world_space_position = thread_uv * 512.0 - 256.0;
	float  feature_size         = 1.0 / 256.0;
	
	float noise = SampleNoise2D<GradientNoiseSampler, 1>(world_space_position * feature_size, 9) * 180.0;
	height_field[thread_id] = noise;
}
