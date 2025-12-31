#ifndef BASIC_HLSL
#define BASIC_HLSL

SamplerState sampler_linear_clamp  : register(s0);
SamplerState sampler_nearest_clamp : register(s1);
SamplerState sampler_linear_wrap   : register(s2);
SamplerState sampler_nearest_wrap  : register(s3);
SamplerState sampler_aniso_wrap    : register(s4);
SamplerState sampler_min_clamp     : register(s5);
SamplerState sampler_max_clamp     : register(s6);


#define compile_const static const
#define ThreadGroupSize(x, y, z) numthreads(x, y, z)

using u16 = uint16_t;
using s16 = int16_t;
using u32 = uint32_t;
using s32 = int32_t;
using u64 = uint64_t;
using s64 = int64_t;
using quat = float4;

#include ROOT_SIGNATURE_FILEPATH


compile_const float PI = 3.1415927;

float  Pow2(float  value) { return value * value; }
float2 Pow2(float2 value) { return value * value; }
float3 Pow2(float3 value) { return value * value; }
float4 Pow2(float4 value) { return value * value; }


//
// Based on https://fgiesen.wordpress.com/2022/09/09/morton-codes-addendum/
// See EncodeMorton2_8b_better and DecodeMorton2_8b.
//
uint MortonEncode(uint2 coordinates) {
	uint t = (coordinates.x & 0xFF) | ((coordinates.y & 0xFF) << 16);
	
	t = (t ^ (t << 4)) & 0x0F0F0F0F;
	t = (t ^ (t << 2)) & 0x33333333;
	t = (t ^ (t << 1)) & 0x55555555;
	
	return (t >> 15) | (t & 0xFFFF);
}

uint2 MortonDecode(uint code) {
	uint t = (code & 0x5555) | ((code & 0xAAAA) << 15);
	
	t = (t ^ (t >> 1)) & 0x33333333;
	t = (t ^ (t >> 2)) & 0x0F0F0F0F;
	t ^= t >> 4;
	
	return uint2(t, t >> 16) & 0xFF;
}

float3 DecodeSRGB(float3 x) { return select(x < 0.04045, (x / 12.92), pow((x + 0.055) / 1.055, 2.4)); }
float3 EncodeSRGB(float3 x) { return select(x < 0.0031308, 12.92 * x, (1.055 * pow(x, 1.0 / 2.4) - 0.055)); }
float4 DecodeSRGB(float4 x) { return float4(DecodeSRGB(x.xyz), x.w); }
float4 EncodeSRGB(float4 x) { return float4(EncodeSRGB(x.xyz), x.w); }

float4 DecodeR8G8B8A8_UNORM(uint encoded) { return float4(uint4(encoded >> 0, encoded >> 8, encoded >> 16, encoded >> 24) & 0xFF) * (1.0 / 255.0); }
float4 DecodeR8G8B8A8_UNORM_SRGB(uint encoded) { return DecodeSRGB(DecodeR8G8B8A8_UNORM(encoded)); }


float2 NdcToScreenUv(float2 ndc) { return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5); }
float2 ScreenUvToNdc(float2 uv)  { return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0); }


float BarycentricWireframe(float3 lambda, float3 lambda_ddx, float3 lambda_ddy, float thickness = 1.0) {
	float3 wireframe = smoothstep(0.0, (abs(lambda_ddx) + abs(lambda_ddy)) * thickness, lambda);
	return min(min(wireframe.x, wireframe.y), wireframe.z);
}


bool IsPerspectiveMatrix(float4 coefficients)  { return coefficients.w == 0.0; }
bool IsOrthographicMatrix(float4 coefficients) { return coefficients.w != 0.0; }

struct RayInfo {
	float3 origin;
	float3 direction;
};

RayInfo RayInfoFromNdc(float2 ndc, float4 clip_to_view_coef) {
	RayInfo result;
	
	if (IsPerspectiveMatrix(clip_to_view_coef)) {
		result.origin    = float3(0.0, 0.0, 0.0);
		result.direction = normalize(float3(ndc * clip_to_view_coef.xy, 1.0));
	} else {
		result.origin    = float3(ndc * clip_to_view_coef.xy, 0.0);
		result.direction = float3(0.0, 0.0, 1.0);
	}
	
	return result;
}

RayInfo RayInfoFromScreenUv(float2 uv, float4 clip_to_view_coef) {
	return RayInfoFromNdc(ScreenUvToNdc(uv), clip_to_view_coef);
}

float4 TransformViewToClipSpace(float3 view_space_position, float4 view_to_clip_coef) {
	float4 result;
	result.xy = view_space_position.xy * view_to_clip_coef.xy;
	
	if (IsPerspectiveMatrix(view_to_clip_coef)) {
		result.z = view_to_clip_coef.z;
		result.w = view_space_position.z;
	} else {
		result.z = view_space_position.z * view_to_clip_coef.z + 1.0;
		result.w = 1.0;
	}
	
	return result;
}

float3 QuatMul(quat q, float3 v) {
	// Rotate a vector with a quaternion. Equivalent to QuatToRotationMatrix(q) * v
	// https://fgiesen.wordpress.com/2019/02/09/rotating-a-single-vector-using-a-quaternion/
	float3 t = cross(q.xyz, v) * 2.0;
	return v + t * q.w + cross(q.xyz, t);
}

#endif // BASIC_HLSL
