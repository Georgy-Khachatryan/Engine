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

template<typename NoiseSamplerT, s32 component_count>
vector<float, component_count> SampleNoise2D(float2 position, u32 octave_count, u32 seed = 0) {
	vector<float, component_count> value = 0.0;
	float weight_sum = 0.0;
	float weight     = 1.0;
	float frequencey = 1.0;
	
	for (u32 octave_index = 0; octave_index < octave_count; octave_index += 1) {
		for (u32 component_index = 0; component_index < component_count; component_index += 1) {
			NoiseSamplerT noise_sampler;
			noise_sampler.seed = WyHash32(seed, component_index);
			
			value[component_index] += noise_sampler.Sample2D(position * frequencey) * weight;
		}
		
		weight_sum += weight;
		weight     *= 0.5;
		frequencey *= 2.0;
	}
	
	return value * rcp(weight_sum);
}

#endif // NOISE_HLSL
