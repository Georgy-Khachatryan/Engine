#include "Basic.hlsl"
#include "Noise.hlsl"
#include "Generated/TerrainEditorData.hlsl"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

float2 WorldSpacePositionToUv(float2 world_space_position) {
	return world_space_position * (1.0 / 512.0) + 0.5;
}

float2 UvToWorldSpacePosition(float2 uv) {
	return uv * 512.0 - 256.0;
}

void TerrainEditorCommandTypeClear(uint2 thread_id) {
	height_field_1[thread_id] = 0.0;
}

void TerrainEditorCommandTypeCopy(uint2 thread_id) {
	height_field_1[thread_id] = height_field_0[thread_id];
}


float2x2 CreateRotationMatrix(float2 cos_sin) {
	float2x2 rotation;
	rotation[0] = float2(cos_sin.x, -cos_sin.y);
	rotation[1] = float2(+cos_sin.y, cos_sin.x);
	return rotation;
}

template<u32 component_count, typename SettingsT>
vector<float, component_count> EvalueateTerrainNoise(SettingsT settings, float2 world_space_position) {
	float2x2 rotation = CreateRotationMatrix(settings.rotation);
	
	float2 scale = float2(1.0 + settings.anisotropy, 1.0 - settings.anisotropy) * settings.inv_scale;
	
	float2 noise_coordinates = mul(rotation, world_space_position) * scale;
	
	vector<float, component_count> noise_value = 0.0;
	switch (settings.type) {
	case TerrainEditorNoiseType::Gradient: {
		noise_value = SampleNoise2D<GradientNoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::GradientBillow: {
		noise_value = SampleNoise2D<GradientBillowNoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::Value: {
		noise_value = SampleNoise2D<ValueNoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiF1: {
		noise_value = SampleNoise2D<VoronoiF1NoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiF2: {
		noise_value = SampleNoise2D<VoronoiF2NoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiF2MinusF1: {
		noise_value = SampleNoise2D<VoronoiF2MinusF1NoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} case TerrainEditorNoiseType::VoronoiID: {
		noise_value = SampleNoise2D<VoronoiIDNoiseSampler, component_count>(noise_coordinates, settings.octave_count, settings.lacunarity, settings.gain, settings.random_seed);
		break;
	} default: {
		
		break;
	}
	}
	
	return noise_value;
}

void TerrainHeightLayerNoise(uint2 thread_id, float2 world_space_position) {
	TerrainHeightLayerNoiseGpuSettings settings = layer_constants.Load<TerrainHeightLayerNoiseGpuSettings>(0);
	
	float2 distortion_coordinates = world_space_position * settings.inv_distortion_scale;
	
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
	
	float noise_value = EvalueateTerrainNoise<1>(settings, world_space_position + distortion_value * settings.distortion_amplitude);
	
	height_field_1[thread_id] = height_field_0[thread_id] + noise_value * settings.amplitude;
}

void TerrainHeightLayerDistortion(uint2 thread_id, float2 world_space_position) {
	TerrainHeightLayerDistortionGpuSettings settings = layer_constants.Load<TerrainHeightLayerDistortionGpuSettings>(0);
	
	float2 noise_value = EvalueateTerrainNoise<2>(settings, world_space_position);
	
	height_field_1[thread_id] = height_field_0.SampleLevel(sampler_linear_clamp, WorldSpacePositionToUv(world_space_position + noise_value * settings.amplitude), 0.0);
}


float ComputeStrataSliceHeight(s32 index, float offset, float randomness, float period, u32 random_seed) {
	return index * period + offset + ((WyHash32(index, random_seed) & 0xFFFF) * rcp(0x10000) - 0.5) * period * randomness;
}

float2 ComputeStrataSliceRange(float height, float frequency, float period, float offset, float randomness, u32 random_seed) {
	s32 index = (s32)floor(height * frequency);
	
	float2 result = 0.0;
	result.x = ComputeStrataSliceHeight(index + 0, offset, randomness, period, random_seed);
	result.y = ComputeStrataSliceHeight(index + 1, offset, randomness, period, random_seed);
	
	if (result.y < height) {
		result.x = result.y;
		result.y = ComputeStrataSliceHeight(index + 2, offset, randomness, period, random_seed);
	} else if (result.x > height) {
		result.y = result.x;
		result.x = ComputeStrataSliceHeight(index - 1, offset, randomness, period, random_seed);
	}
	
	return result;
}

void TerrainHeightLayerStrata(uint2 thread_id, float2 world_space_position) {
	TerrainHeightLayerStrataGpuSettings settings = layer_constants.Load<TerrainHeightLayerStrataGpuSettings>(0);
	
	float2x2 tilt     = CreateRotationMatrix(settings.tilt);
	float2x2 rotation = CreateRotationMatrix(settings.rotation);
	
	float height = height_field_0[thread_id];
	
	float3 position = float3(world_space_position, height);
	position.xy = mul(rotation, position.xy);
	position.xz = mul(tilt,     position.xz);
	
	float frequency = settings.inv_period;
	float weight    = 1.0;
	for (u32 octave = 0; octave < settings.octave_count; octave += 1) {
		u32 seed = settings.random_seed * 16 + octave;
		
		float period = 1.0 / frequency;
		float  noise = SampleNoise2D<GradientNoiseSampler, 1>(position.xy * frequency * settings.inv_distortion_scale, 1, 2.0, 0.5, seed) * settings.distortion_amount * period;
		float2 range = ComputeStrataSliceRange(position.z, frequency, period, noise, settings.randomness, seed);
		
		position.z = lerp(position.z, lerp(range.x, range.y, smoothstep(range.x, range.y, position.z)), weight);
		
		frequency *= settings.lacunarity;
		weight    *= settings.gain;
	}
	
	position.xz = mul(transpose(tilt),     position.xz);
	position.xy = mul(transpose(rotation), position.xy);
	
	height_field_1[thread_id] = lerp(height, position.z, settings.amount);
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * constants.inv_render_target_size;
	
	float2 world_space_position = UvToWorldSpacePosition(thread_uv);
	
	switch (constants.command_type) {
	case TerrainEditorCommandType::Clear: {
		TerrainEditorCommandTypeClear(thread_id);
		break;
	} case TerrainEditorCommandType::Copy: {
		TerrainEditorCommandTypeCopy(thread_id);
		break;
	} case TerrainEditorCommandType::TerrainHeightLayerNoise: {
		TerrainHeightLayerNoise(thread_id, world_space_position);
		break;
	} case TerrainEditorCommandType::TerrainHeightLayerDistortion: {
		TerrainHeightLayerDistortion(thread_id, world_space_position);
		break;
	} case TerrainEditorCommandType::TerrainHeightLayerStrata: {
		TerrainHeightLayerStrata(thread_id, world_space_position);
		break;
	} default: {
		
		break;
	}
	}
}
