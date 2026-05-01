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
using float16 = float16_t;
using float32 = float;
using float64 = double;
using float16x2 = float16_t2;
using float16x3 = float16_t3;
using float16x4 = float16_t4;
using u16x2 = uint16_t2;
using u16x3 = uint16_t3;
using u16x4 = uint16_t4;
using s32x2 = int2;
using s32x3 = int3;
using s32x4 = int4;
using s16x2 = int16_t2;
using s16x3 = int16_t3;
using s16x4 = int16_t4;
using quat = float4;

compile_const u16 u16_max = (u16)0xFFFF;
compile_const u16 u16_min = (u16)0x0000;
compile_const s16 s16_max = (s16)0x7FFF;
compile_const s16 s16_min = (s16)0x8000;
compile_const u32 u32_max = (u32)0xFFFFFFFF;
compile_const u32 u32_min = (u32)0x00000000;
compile_const s32 s32_max = (s32)0x7FFFFFFF;
compile_const s32 s32_min = (s32)0x80000000;
compile_const u64 u64_max = (u64)0xFFFFFFFFFFFFFFFF;
compile_const u64 u64_min = (u64)0x0000000000000000;
compile_const s64 s64_max = (s64)0x7FFFFFFFFFFFFFFF;
compile_const s64 s64_min = (s64)0x8000000000000000;


#define BEGIN_ROOT_SIGNATURE_NAMESPACE(pass_name, scope_name) namespace pass_name { namespace scope_name {
#define END_ROOT_SIGNATURE_NAMESPACE(pass_name, scope_name) } }

#define NV_SHADER_EXTN_SLOT u0
#define NV_SHADER_EXTN_REGISTER_SPACE space1

#include ROOT_SIGNATURE_FILEPATH


compile_const float PI = 3.1415927;

float  Pow2(float  value) { return value * value; }
float2 Pow2(float2 value) { return value * value; }
float3 Pow2(float3 value) { return value * value; }
float4 Pow2(float4 value) { return value * value; }

float  Pow5(float  value) { return Pow2(Pow2(value)) * value; }
float2 Pow5(float2 value) { return Pow2(Pow2(value)) * value; }
float3 Pow5(float3 value) { return Pow2(Pow2(value)) * value; }
float4 Pow5(float4 value) { return Pow2(Pow2(value)) * value; }

uint DivideAndRoundUp(uint numerator, uint denominator) { return (numerator + (denominator - 1)) / denominator; }
uint AlignUp(uint size, uint alignment) { return (size + alignment - 1) & ~(alignment - 1); }

template<typename T>
T WaveShuffleXor(T value, uint mask) { return WaveReadLaneAt(value, WaveGetLaneIndex() ^ mask); }


// Based on wyhash32 by Wang Yi https://github.com/wangyi-fudan/wyhash/blob/master/wyhash32.h (public domain).
void WyMix32(inout u32 A, inout u32 B) {
	u64 c = (u64)(A ^ 0x53C5CA59u) * (u64)(B ^ 0x74743C1Bu);
	A = (u32)(c >>  0);
	B = (u32)(c >> 32);
}

u32 WyHash32(u32 a, u32 b) {
	WyMix32(a, b);
	WyMix32(a, b);
	return a ^ b;
}


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

float4 DecodeR16G16B16A16_SNORM(s16x4 encoded) { return float4(encoded) * (1.0 / s16_max); }
float2 DecodeR16G16_SNORM(s16x2 encoded) { return float2(encoded) * (1.0 / s16_max); }

float2 NdcToScreenUv(float2 ndc) { return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5); }
float2 ScreenUvToNdc(float2 uv)  { return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0); }

float2 NdcToScreenUvDirection(float2 ndc) { return float2(ndc.x * 0.5, ndc.y * -0.5); }
float2 ScreenUvToNdcDirection(float2 uv)  { return float2(uv.x * 2.0, uv.y * -2.0); }

float2 LutParametersToUv(float2 parameters, uint2 lut_size) { return (parameters * (lut_size - 1) + 0.5) * (1.0 / lut_size); }

// Based on https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 EncodeOctahedralMap(float3 value) {
	float2 result = value.xy * (1.0 / (abs(value.x) + abs(value.y) + abs(value.z)));
	return select(value.z >= 0.0, result, (1.0 - abs(result.yx)) * select(result.xy >= 0.0, 1.0, -1.0));
}
 
// Optimized octahedron decoding by Rune Stubbe.
template<typename T>
vector<T, 3> DecodeOctahedralMap(vector<T, 2> value) {
	vector<T, 3> result = vector<T, 3>(value.x, value.y, (T)1.0 - abs(value.x) - abs(value.y));
	T t = saturate(-result.z);
	return normalize(vector<T, 3>(result.xy + select(result.xy >= (T)0.0, -t, t), result.z));
}

// Based on https://x.com/rygorous/status/1292942936817115136
float2 EncodeHemiOctahedralMap(float3 value) {
	float2 t = value.xy * (1.0 / (abs(value.x) + abs(value.y) + abs(value.z)));
	return float2(t.x + t.y, t.x - t.y);
}

template<typename T>
vector<T, 3> DecodeHemiOctahedralMap(vector<T, 2> value) {
	vector<T, 2> t = vector<T, 2>(value.x + value.y, value.x - value.y);
	return normalize(vector<T, 3>(t, 2.0 - abs(t.x) - abs(t.y)));
}

template<typename T>
vector<T, 3> DecodeHemiOctahedralMap01(vector<T, 2> value) { // Same as DecodeHemiOctahedralMap, but input is in [0, 1] range instead of [-1, 1]
	vector<T, 2> t = vector<T, 2>(value.x + value.y - (T)1.0, value.x - value.y);
	return normalize(vector<T, 3>(t, (T)1.0 - abs(t.x) - abs(t.y)));
}


float BarycentricWireframe(float3 lambda, float3 lambda_ddx, float3 lambda_ddy, float thickness = 1.0) {
	float3 wireframe = smoothstep(0.0, (abs(lambda_ddx) + abs(lambda_ddy)) * thickness, lambda);
	return min(min(wireframe.x, wireframe.y), wireframe.z);
}

// Based on matplotlib colormaps by mattz https://www.shadertoy.com/view/WlfXRN (public domain).
float3 ViridisHeatMapSRGB(float t) {
	float3 c0 = float3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
	float3 c1 = float3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
	float3 c2 = float3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
	float3 c3 = float3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
	float3 c4 = float3(6.228269936347081, 14.17993336680509, 56.69055260068105);
	float3 c5 = float3(4.776384997670288, -13.74514537774601, -65.35303263337234);
	float3 c6 = float3(-5.435455855934631, 4.645852612178535, 26.3124352495832);
	return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

float3 PlasmaHeatMapSRGB(float t) {
	float3 c0 = float3(0.05873234392399702, 0.02333670892565664, 0.5433401826748754);
	float3 c1 = float3(2.176514634195958, 0.2383834171260182, 0.7539604599784036);
	float3 c2 = float3(-2.689460476458034, -7.455851135738909, 3.110799939717086);
	float3 c3 = float3(6.130348345893603, 42.3461881477227, -28.51885465332158);
	float3 c4 = float3(-11.10743619062271, -82.66631109428045, 60.13984767418263);
	float3 c5 = float3(10.02306557647065, 71.41361770095349, -54.07218655560067);
	float3 c6 = float3(-3.658713842777788, -22.93153465461149, 18.19190778539828);
	return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

float3 ViridisHeatMap(float t) {
	return DecodeSRGB(ViridisHeatMapSRGB(t));
}

float3 PlasmaHeatMap(float t) {
	return DecodeSRGB(PlasmaHeatMapSRGB(t));
}

// http://lolengine.net/blog/2013/07/27/rgb-to-hsv-in-glsl
float3 ConvertHSVtoRGB(float3 hsv) {
	float3 hue = abs(frac(hsv.x + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0;
	return lerp(1.0, saturate(hue), hsv.y) * hsv.z;
}

float3 RandomColor(uint seed_a, uint seed_b = 0) {
	u32 hash = WyHash32(seed_a, seed_b);
	return ConvertHSVtoRGB(float3((hash & 0xFF) * rcp(0xFF), 0.6, 0.7));
}


float2 ConcentricMapping(float2 uv) {
	float2 ndc = uv * 2.0 - 1.0;
	if (all(ndc == 0.0)) return 0.0;
	
	float theta  = 0.0;
	float radius = 1.0;
	
	if (abs(ndc.x) > abs(ndc.y)) { 
		radius = ndc.x;
		theta  = (PI * 0.25) * (ndc.y / ndc.x);
	} else {
		radius = ndc.y;
		theta  = (PI * 0.5) - ((PI * 0.25) * (ndc.x / ndc.y)); 
	}
	
	return float2(cos(theta), sin(theta)) * radius;
}

float3 CosineWeightedHemisphereMapping(float2 uv) {
	float2 disk_sample = ConcentricMapping(uv);
	return float3(disk_sample, sqrt(max(1.0 - Pow2(disk_sample.x) - Pow2(disk_sample.y), 0.0)));
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

// See BasicMath.cpp for reference on view_to_clip coefficients.
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

float4 TransformViewToClipSpaceDirection(float3 view_space_position, float4 view_to_clip_coef) {
	float4 result;
	result.xy = view_space_position.xy * view_to_clip_coef.xy;
	
	if (IsPerspectiveMatrix(view_to_clip_coef)) {
		result.z = 0.0;
		result.w = view_space_position.z;
	} else {
		result.z = view_space_position.z * view_to_clip_coef.z;
		result.w = 0.0;
	}
	
	return result;
}

float3 TransformNdcToViewSpace(float2 ndc, float clip_space_depth, float4 clip_to_view_coef) {
	float3 result;
	
	if (IsPerspectiveMatrix(clip_to_view_coef)) {
		float view_space_depth = rcp(clip_space_depth * clip_to_view_coef.z);
		result = float3(ndc * clip_to_view_coef.xy * view_space_depth, view_space_depth);
	} else {
		result.xy = ndc * clip_to_view_coef.xy;
		result.z  = clip_space_depth * clip_to_view_coef.z - clip_to_view_coef.z;
	}
	
	return result;
}

float3 TransformScreenUvToViewSpace(float2 uv, float clip_space_depth, float4 clip_to_view_coef, float2 jitter_offset_ndc = 0.0) {
	return TransformNdcToViewSpace(ScreenUvToNdc(uv) - jitter_offset_ndc, clip_space_depth, clip_to_view_coef);
}


// Tom Duff, James Burgess, Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, and Ryusuke Villemin.
// 2017. Building an Orthonormal Basis, Revisited. https://jcgt.org/published/0006/01/01/
float3x3 BuildOrthonormalBasis(float3 normal) {
	float sign = normal.z < 0.f ? -1.0 : +1.0;
	
	float a = -1.0 / (sign + normal.z);
	float b = normal.x * normal.y * a;
	
	float3x3 result;
	result[0] = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
	result[1] = float3(b, sign + normal.y * normal.y * a, -normal.y);
	result[2] = normal;
	
	return result;
}

float3 QuatMul(quat q, float3 v) {
	// Rotate a vector with a quaternion. Equivalent to QuatToRotationMatrix(q) * v
	// https://fgiesen.wordpress.com/2019/02/09/rotating-a-single-vector-using-a-quaternion/
	float3 t = cross(q.xyz, v) * 2.0;
	return v + t * q.w + cross(q.xyz, t);
}

float3x3 QuatToRotationMatrix(quat q) {
	float3x3 result;
	result[0] = float3(1.0 - 2.0 * (q.y * q.y + q.z * q.z),       2.0 * (q.x * q.y - q.z * q.w),       2.0 * (q.x * q.z + q.y * q.w));
	result[1] = float3(      2.0 * (q.x * q.y + q.z * q.w), 1.0 - 2.0 * (q.x * q.x + q.z * q.z),       2.0 * (q.y * q.z - q.x * q.w));
	result[2] = float3(      2.0 * (q.x * q.z - q.y * q.w),       2.0 * (q.y * q.z + q.x * q.w), 1.0 - 2.0 * (q.x * q.x + q.y * q.y));
	return result;
}

#if defined(GENERATED_MESHDATA_HLSL)
float3 TransformModelToWorldSpace(float3 model_space_position, GpuTransform model_to_world) {
	return QuatMul(model_to_world.rotation, model_space_position * model_to_world.scale) + model_to_world.position;
}

float3 TransformModelToWorldSpaceDirection(float3 model_space_direction, GpuTransform model_to_world) {
	return QuatMul(model_to_world.rotation, model_space_direction * model_to_world.scale);
}

float4 TransformModelToClipSpace(float3 model_space_position, GpuTransform model_to_world, float3x4 world_to_view, float4 view_to_clip_coef) {
	float3 world_space_position = TransformModelToWorldSpace(model_space_position, model_to_world);
	float3 view_space_position  = mul(world_to_view, float4(world_space_position, 1.0));
	float4 clip_space_position  = TransformViewToClipSpace(view_space_position, view_to_clip_coef);
	
	return clip_space_position;
}

float4 TransformModelToClipSpaceDirection(float3 model_space_direction, GpuTransform model_to_world, float3x4 world_to_view, float4 view_to_clip_coef) {
	float3 world_space_direction = TransformModelToWorldSpaceDirection(model_space_direction, model_to_world);
	float3 view_space_direction  = mul((float3x3)world_to_view, world_space_direction);
	float4 clip_space_direction  = TransformViewToClipSpaceDirection(view_space_direction, view_to_clip_coef);
	
	return clip_space_direction;
}
#endif // defined(GENERATED_MESHDATA_HLSL)


bool BitArrayTestBit(StructuredBuffer<uint> mask, u32 index, u32 offset = 0) { return (mask[offset + index / 32u] & (1u << (index % 32u))) != 0; }
void BitArraySetBit(RWStructuredBuffer<uint> mask, u32 index, u32 offset = 0) { InterlockedOr(mask[offset + index / 32u], 1u << (index % 32u)); }
void BitArrayResetBit(RWStructuredBuffer<uint> mask, u32 index, u32 offset = 0) { InterlockedAnd(mask[offset + index / 32u], ~(1u << (index % 32u))); }

#define GsBitArrayTestBit(gs_mask, index) ((gs_mask[index / 32u] & (1u << (index % 32u))) != 0)
#define GsBitArraySetBit(gs_mask, index) InterlockedOr(gs_mask[index / 32u], 1u << (index % 32u))
#define GsBitArrayResetBit(gs_mask, index) InterlockedAnd(gs_mask[index / 32u], ~(1u << (index % 32u)))

uint CreateBitMaskSmall(uint bit_count) {
	return ((1u << bit_count) - 1);
}

uint CreateBitMask(uint bit_count) {
	return bit_count >= 32 ? u32_max : CreateBitMaskSmall(bit_count);
}

#endif // BASIC_HLSL
