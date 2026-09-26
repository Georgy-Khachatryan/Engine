#include "Basic.hlsl"
#include "Noise.hlsl"
#include "Generated/TerrainEditorData.hlsl"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;
compile_const float height_field_extent = 512.0;

float2 WorldSpacePositionToUv(float2 world_space_position) {
	return world_space_position * (1.0 / height_field_extent) + 0.5;
}

float2 UvToWorldSpacePosition(float2 uv) {
	return uv * height_field_extent - height_field_extent * 0.5;
}

s32x2 UvToTexelPosition(float2 uv) {
	return uv * constants.render_target_size;
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

void TerrainHeightLayerErosionClear(uint2 thread_id) {
	flow_field_1[thread_id] = 0.0;
	flow_field_x_1[thread_id] = 0;
	flow_field_y_1[thread_id] = 0;
	flow_field_w_1[thread_id] = 0;
	erosion_field_1[thread_id] = 0;
}

float2 SampleHeightFieldGradient(float2 uv, float mip_index = 0.0) {
	float dx =
		height_field_0.SampleLevel(sampler_linear_clamp, uv, mip_index, s32x2(-1, 0)) -
		height_field_0.SampleLevel(sampler_linear_clamp, uv, mip_index, s32x2(+1, 0));
	
	float dy =
		height_field_0.SampleLevel(sampler_linear_clamp, uv, mip_index, s32x2(0, -1)) -
		height_field_0.SampleLevel(sampler_linear_clamp, uv, mip_index, s32x2(0, +1));
	
	return float2(dx, dy) / (2.0 * height_field_extent * constants.inv_render_target_size);
}

compile_const float fixed_point_scale     = 16.0 * 1024.0;
compile_const float inv_fixed_point_scale = 1.0 / fixed_point_scale;

void TerrainHeightLayerErosionSimulate(uint2 thread_id) {
	TerrainHeightLayerErosionGpuSettings settings = layer_constants.Load<TerrainHeightLayerErosionGpuSettings>(0);
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), settings.random_seed);
	
	float2 thread_uv = (thread_id + ComputeRandomUnorm16x2(hash)) * 4.0 * constants.inv_render_target_size;
	float2 position  = UvToWorldSpacePosition(thread_uv);
	
	float2 velocity = SampleHeightFieldGradient(thread_uv);
	
	float sediment = 0.0;
	float water    = settings.fluvial_initial_water;
	float height   = height_field_0.SampleLevel(sampler_linear_clamp, thread_uv, 0.0);
	
	float texel_size_meters = height_field_extent * constants.inv_render_target_size;
	
	u32 step_count = 128;
	for (u32 i = 0; i < step_count && any(float2(sediment, water) > (1.0 / 1024.0)); i += 1) {
		float2 sample_position = position;
		float2 sample_uv = WorldSpacePositionToUv(sample_position);
		s32x2  sample_id = UvToTexelPosition(sample_uv);
		
		if (any(sample_id < 0) || any(sample_id >= (s32)constants.render_target_size)) break;
		
		float2 flow_map = flow_field_0.SampleLevel(sampler_linear_clamp, sample_uv, 0.0);
		float2 gradient = SampleHeightFieldGradient(sample_uv);
		
		velocity = lerp(lerp(gradient, velocity, pow(settings.fluvial_inertia, texel_size_meters)), flow_map, settings.fluvial_viscosity);
		float velocity_length = length(velocity);
		
		if (velocity_length < texel_size_meters * (1.0 / 1024.0)) break;
		float2 direction = velocity / velocity_length;
		
		position += direction * texel_size_meters;
		float new_height   = height_field_0.SampleLevel(sampler_linear_clamp, WorldSpacePositionToUv(position), 0.0);
		float delta_height = new_height - height;
		
		float sediment_capacity = max(max(-delta_height, 0.0) * water, 0.0);
		
		InterlockedAdd(flow_field_x_1[sample_id], (s32)(water * fixed_point_scale * velocity.x));
		InterlockedAdd(flow_field_y_1[sample_id], (s32)(water * fixed_point_scale * velocity.y));
		InterlockedAdd(flow_field_w_1[sample_id], (u32)(water * fixed_point_scale));
		
		s32 erosion_delta = 0;
		if (sediment > sediment_capacity || delta_height > 0.0) {
			float deposition_rate = settings.fluvial_deposition_rate;
			float amount_to_deposit = clamp(delta_height > 0.0 ? min(delta_height, (sediment - sediment_capacity) * deposition_rate) : (sediment - sediment_capacity) * deposition_rate, 0.0, sediment);
			sediment -= amount_to_deposit;
			
			erosion_delta = (s32)(amount_to_deposit * fixed_point_scale);
		} else {
			float erosion_rate = settings.fluvial_erosion_rate;
			float amount_to_erode = clamp(min((sediment_capacity - sediment) * erosion_rate, -delta_height), 0.0, sediment_capacity);
			sediment += amount_to_erode;
			
			erosion_delta = (s32)(-amount_to_erode * fixed_point_scale);
		}
		
		if (erosion_delta != 0) {
			InterlockedAdd(erosion_field_1[sample_id], erosion_delta);
		}
		
		height = new_height;
		water *= saturate(1.0 - settings.fluvial_evaporation_rate);
	}
	
	if (sediment > 0.0) {
		float2 sample_uv = WorldSpacePositionToUv(position);
		s32x2  sample_id = UvToTexelPosition(sample_uv);
		if (all(sample_id >= 0) && all(sample_id < constants.render_target_size)) {
			InterlockedAdd(erosion_field_1[sample_id], (s32)(sediment * fixed_point_scale));
		}
	}
}

void TerrainHeightLayerErosionApply(uint2 thread_id) {
	float3 flow_map = float3(flow_field_x_1[thread_id], flow_field_y_1[thread_id], flow_field_w_1[thread_id]);
	if (flow_map.z > 0.0) {
		flow_field_1[thread_id] = flow_map.xy * rcp(flow_map.z);
	}
	
	float texel_size_meters = height_field_extent * constants.inv_render_target_size;
	float erosion = clamp((float)erosion_field_1[thread_id] * inv_fixed_point_scale, -texel_size_meters, +texel_size_meters);
	erosion_field_1[thread_id] = 0;
	
	height_field_1[thread_id] = height_field_0[thread_id] + erosion;
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
	} case TerrainEditorCommandType::TerrainHeightLayerErosionClear: {
		TerrainHeightLayerErosionClear(thread_id);
		break;
	} case TerrainEditorCommandType::TerrainHeightLayerErosionSimulate: {
		TerrainHeightLayerErosionSimulate(thread_id);
		break;
	} case TerrainEditorCommandType::TerrainHeightLayerErosionApply: {
		TerrainHeightLayerErosionApply(thread_id);
		break;
	} default: {
		
		break;
	}
	}
}
