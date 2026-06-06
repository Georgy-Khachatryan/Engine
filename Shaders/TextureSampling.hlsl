#ifndef TEXTURESAMPLING_HLSL
#define TEXTURESAMPLING_HLSL
#include "Basic.hlsl"


// An HLSL function for sampling a 2D texture with Catmull-Rom filtering, using 9 texture samples instead of 16.
// The following code is licensed under the MIT license, see license in THIRD_PARTY_LICENSES.md
float4 SampleTextureCatmullRom(Texture2D<float4> src_texture, SamplerState src_sampler, float2 uv, float mip_level, float2 texture_size, float2 inv_texture_size) {
	// We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
	// down the sample location to get the exact center of our "starting" texel. The starting texel will be at
	// location [1, 1] in the grid, where [0, 0] is the top left corner.
	float2 sample_pos = uv * texture_size;
	float2 tex_pos_1  = floor(sample_pos - 0.5) + 0.5;
	
	// Compute the fractional offset from our starting texel to our original sample location, which we'll
	// feed into the Catmull-Rom spline function to get our filter weights.
	float2 f = sample_pos - tex_pos_1;
	
	// Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
	// These equations are pre-expanded based on our knowledge of where the texels will be located,
	// which lets us avoid having to evaluate a piece-wise function.
	float2 w0 = f * (-0.5f + f * (1.0 - 0.5 * f));
	float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);
	
	// Work out weighting factors and sampling offsets that will let us use bilinear filtering to
	// simultaneously evaluate the middle 2 samples from the 4x4 grid.
	float2 w12 = w1 + w2;
	float2 offset_12 = w2 / (w1 + w2);
	
	// Compute the final UV coordinates we'll use for sampling the texture.
	float2 tex_pos_0  = (tex_pos_1 - 1.0) * inv_texture_size;
	float2 tex_pos_3  = (tex_pos_1 + 2.0) * inv_texture_size;
	float2 tex_pos_12 = (tex_pos_1 + offset_12) * inv_texture_size;
	
	float4 result = 0.0;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_0.x,  tex_pos_0.y), mip_level) * w0.x  * w0.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_12.x, tex_pos_0.y), mip_level) * w12.x * w0.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_3.x,  tex_pos_0.y), mip_level) * w3.x  * w0.y;
	
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_0.x,  tex_pos_12.y), mip_level) * w0.x  * w12.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_12.x, tex_pos_12.y), mip_level) * w12.x * w12.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_3.x,  tex_pos_12.y), mip_level) * w3.x  * w12.y;
	
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_0.x,  tex_pos_3.y), mip_level) * w0.x  * w3.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_12.x, tex_pos_3.y), mip_level) * w12.x * w3.y;
	result += src_texture.SampleLevel(src_sampler, float2(tex_pos_3.x,  tex_pos_3.y), mip_level) * w3.x  * w3.y;
	
	return result;
}


float ComputeGaussianWeight(float x, float y, float radius) {
	return exp(-3.0 * (float)(Pow2(x) + Pow2(y)) / Pow2(radius + 1.0));
}


// https://www.reedbeta.com/blog/texture-gathers-and-coordinate-precision/
float4 ComputeBilinearWeights(float2 pixel_coordinates) {
	float2 fractional = frac(pixel_coordinates + (-0.5 + 1.0 / 512.0));
	
	float4 bilinear_weights;
	bilinear_weights.x = (1.0 - fractional.x) * (1.0 - fractional.y);
	bilinear_weights.y = fractional.x * (1.0 - fractional.y);
	bilinear_weights.z = (1.0 - fractional.x) * fractional.y;
	bilinear_weights.w = fractional.x * fractional.y;
	
	return bilinear_weights;
}

float2 ComputeBilinearSamplePixelCoordinates(float2 pixel_coordinates) {
	return floor(pixel_coordinates + (-0.5 + 1.0 / 512.0));
}

template<uint channel_index, typename T>
vector<T, 4> GatherChannel(Texture2D<T> src_texture, SamplerState src_sampler, float2 uv, s32x2 offset = 0) {
	switch (channel_index) {
	case 0: return src_texture.GatherRed(src_sampler, uv, offset).wzxy;
	}
	_Static_assert(channel_index < 1, "Invalid GatherChannel channel_index.");
}

template<uint channel_index, typename T>
vector<T, 4> GatherChannel(Texture2D<vector<T, 2> > src_texture, SamplerState src_sampler, float2 uv, s32x2 offset = 0) {
	switch (channel_index) {
	case 0: return src_texture.GatherRed(src_sampler, uv, offset).wzxy;
	case 1: return src_texture.GatherGreen(src_sampler, uv, offset).wzxy;
	}
	_Static_assert(channel_index < 2, "Invalid GatherChannel channel_index.");
}

template<uint channel_index, typename T>
vector<T, 4> GatherChannel(Texture2D<vector<T, 3> > src_texture, SamplerState src_sampler, float2 uv, s32x2 offset = 0) {
	switch (channel_index) {
	case 0: return src_texture.GatherRed(src_sampler, uv, offset).wzxy;
	case 1: return src_texture.GatherGreen(src_sampler, uv, offset).wzxy;
	case 2: return src_texture.GatherBlue(src_sampler, uv, offset).wzxy;
	}
	_Static_assert(channel_index < 3, "Invalid GatherChannel channel_index.");
}

template<uint channel_index, typename T>
vector<T, 4> GatherChannel(Texture2D<vector<T, 4> > src_texture, SamplerState src_sampler, float2 uv, s32x2 offset = 0) {
	switch (channel_index) {
	case 0: return src_texture.GatherRed(src_sampler, uv, offset).wzxy;
	case 1: return src_texture.GatherGreen(src_sampler, uv, offset).wzxy;
	case 2: return src_texture.GatherBlue(src_sampler, uv, offset).wzxy;
	case 3: return src_texture.GatherAlpha(src_sampler, uv, offset).wzxy;
	}
	_Static_assert(channel_index < 4, "Invalid GatherChannel channel_index.");
}

#endif // TEXTURESAMPLING_HLSL
