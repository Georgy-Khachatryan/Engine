#include "Basic.hlsl"
#include "Noise.hlsl"
#include "Generated/TerrainEditorData.hlsl"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

void TerrainHeightLayerNoise(uint2 thread_id, float2 world_space_position) {
	TerrainHeightLayerNoiseGpuSettings settings = layer_constants.Load<TerrainHeightLayerNoiseGpuSettings>(constants.layer_constants_offset);
	
	float2 distortion_coordinates = world_space_position * settings.ivn_distortion_scale;
	
	float2 distortion_value = 0.0;
	switch (settings.distortion_type) {
	case TerrainEditorDistortionType::None: {
		
		break;
	} case TerrainEditorDistortionType::Gradient: {
		distortion_value = SampleNoise2D<GradientNoiseSampler, 2>(distortion_coordinates, settings.distortion_octave_count, 2.0, 0.5, ~settings.random_seed);
		break;
	} default: {
		
		break;
	}
	}
	
	float2x2 rotation;
	rotation[0] = float2(settings.rotation.x, -settings.rotation.y);
	rotation[1] = float2(+settings.rotation.y, settings.rotation.x);
	
	float2 scale = float2(1.0 + settings.anisotropy, 1.0 - settings.anisotropy) * settings.inv_scale;
	
	float2 noise_coordinates = mul(rotation, world_space_position + distortion_value * settings.distortion_amplitude) * scale;
	
	float noise_value = 0.0;
	switch (settings.type) {
	case TerrainEditorNoiseType::Gradient: {
		noise_value = SampleNoise2D<GradientNoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::GradientBillow: {
		noise_value = SampleNoise2D<GradientBillowNoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::Value: {
		noise_value = SampleNoise2D<ValueNoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiF1: {
		noise_value = SampleNoise2D<VoronoiF1NoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiF2: {
		noise_value = SampleNoise2D<VoronoiF2NoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiF2MinusF1: {
		noise_value = SampleNoise2D<VoronoiF2MinusF1NoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiID: {
		noise_value = SampleNoise2D<VoronoiIDNoiseSampler, 1>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} default: {
		
		break;
	}
	}
	
	height_field[thread_id] = noise_value * settings.amplitude;
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * constants.inv_render_target_size;
	
	float2 world_space_position = thread_uv * 512.0 - 256.0;
	
	TerrainHeightLayerNoise(thread_id, world_space_position);
}
