#ifndef NOISE_HLSL
#define NOISE_HLSL
#include "Basic.hlsl"
#include "TextureSampling.hlsl"

struct NoiseSampler {
	u32 seed;
};

struct GradientNoiseSampler : NoiseSampler {
	float Sample2D(float2 position) {
		float2 cell_fraction = SmootherStepShape(frac(position));
		float2 cell_position = floor(position);
		
		float4 bilinear_weights = ComputeBilinearWeightsFromFractional(cell_fraction);
		
		float value = 0.0;
		for (u32 y = 0; y < 2; y += 1) {
			for (u32 x = 0; x < 2; x += 1) {
				s32x2 sample_position = (s32x2)cell_position + s32x2(x, y);
				u32 hash = WyHash32(sample_position.x, WyHash32(sample_position.y, seed));
				
				float2 gradient = ComputeRandomUnorm16x2(hash) * 2.0 - 1.0;
				
				float cell_weight = bilinear_weights[y * 2 + x];
				float cell_value  = dot(gradient, position - sample_position);
				
				value += cell_value * cell_weight;
			}
		}
		
		return value;
	}
};

struct GradientBillowNoiseSampler : GradientNoiseSampler {
	float Sample2D(float2 position) {
		float value = GradientNoiseSampler::Sample2D(position);
		return abs(value);
	}
};

struct ValueNoiseSampler : NoiseSampler {
	float Sample2D(float2 position) {
		float2 cell_fraction = SmootherStepShape(frac(position));
		float2 cell_position = floor(position);
		
		float4 bilinear_weights = ComputeBilinearWeightsFromFractional(cell_fraction);
		
		float value = 0.0;
		for (u32 y = 0; y < 2; y += 1) {
			for (u32 x = 0; x < 2; x += 1) {
				s32x2 sample_position = (s32x2)cell_position + s32x2(x, y);
				u32 hash = WyHash32(sample_position.x, WyHash32(sample_position.y, seed));
				
				float cell_weight = bilinear_weights[y * 2 + x];
				float cell_value  = ComputeRandomUnorm16x2(hash).x * 2.0 - 1.0;
				
				value += cell_value * cell_weight;
			}
		}
		
		return value;
	}
};

struct VoronoiBaseNoiseSampler : NoiseSampler {
	float4 Sample2D(float2 position) {
		float2 cell_position = floor(position);
		
		float f1 = asfloat(0x7F7FFFFF);
		float f2 = asfloat(0x7F7FFFFF);
		float value_f1 = 0.0;
		float value_f2 = 0.0;
		
		for (s32 y = -1; y <= 1; y += 1) {
			for (s32 x = -1; x <= 1; x += 1) {
				s32x2 sample_position = (s32x2)cell_position + s32x2(x, y);
				u32 hash = WyHash32(sample_position.x, WyHash32(sample_position.y, seed));
				
				float cell_distance = Length2((float2)sample_position + ComputeRandomUnorm16x2(hash) - position);
				float cell_value    = ComputeRandomUnorm16x2(hash).x * 2.0 - 1.0;
				
				if (cell_distance < f1) {
					f2       = f1;
					value_f2 = value_f1;
					
					f1       = cell_distance;
					value_f1 = cell_value;
				} else if (cell_distance < f2) {
					f2       = cell_distance;
					value_f2 = cell_value;
				}
			}
		}
		
		return float4(sqrt(f1), sqrt(f2), value_f1, value_f2);
	}
};

struct VoronoiF1NoiseSampler : VoronoiBaseNoiseSampler {
	float Sample2D(float2 position) {
		float4 value = VoronoiBaseNoiseSampler::Sample2D(position);
		return value.x;
	}
};

struct VoronoiF2NoiseSampler : VoronoiBaseNoiseSampler {
	float Sample2D(float2 position) {
		float4 value = VoronoiBaseNoiseSampler::Sample2D(position);
		return value.y;
	}
};

struct VoronoiF2MinusF1NoiseSampler : VoronoiBaseNoiseSampler {
	float Sample2D(float2 position) {
		float4 value = VoronoiBaseNoiseSampler::Sample2D(position);
		return value.y - value.x;
	}
};

struct VoronoiIDNoiseSampler : VoronoiBaseNoiseSampler {
	float Sample2D(float2 position) {
		float4 value = VoronoiBaseNoiseSampler::Sample2D(position);
		return value.z;
	}
};

template<typename NoiseSamplerT, s32 component_count>
vector<float, component_count> SampleNoise2D(float2 position, u32 octave_count, float lacunarity, float gain, u32 seed) {
	vector<float, component_count> value = 0.0;
	float weight_sum = 0.0;
	float weight     = 1.0;
	float frequency  = 1.0;
	
	for (u32 octave_index = 0; octave_index < octave_count; octave_index += 1) {
		for (u32 component_index = 0; component_index < component_count; component_index += 1) {
			NoiseSamplerT noise_sampler;
			noise_sampler.seed = WyHash32(seed, octave_index * component_count + component_index);
			
			value[component_index] += noise_sampler.Sample2D(position * frequency) * weight;
		}
		
		weight_sum += weight;
		weight     *= gain;
		frequency  *= lacunarity;
	}
	
	return value * rcp(weight_sum);
}

#endif // NOISE_HLSL
