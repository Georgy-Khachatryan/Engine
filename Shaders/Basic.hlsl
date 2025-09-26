#ifndef BASIC_HLSL
#define BASIC_HLSL

#define compile_const static const
#define ThreadGroupSize(x, y, z) numthreads(x, y, z)

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
	
	t = (t ^ (t <<  4)) & 0x0F0F0F0F;
	t = (t ^ (t <<  2)) & 0x33333333;
	t = (t ^ (t <<  1)) & 0x55555555;
	
	return (t >> 15) | (t & 0xFFFF);
}

uint2 MortonDecode(uint code) {
	uint t = (code & 0x5555) | ((code & 0xAAAA) << 15);
	
	t = (t ^ (t >> 1)) & 0x33333333;
	t = (t ^ (t >> 2)) & 0x0f0f0f0f;
	t ^= t >> 4;
	
	return uint2(t, t >> 16) & 0xFF;
}

#endif // BASIC_HLSL
