#pragma once
#include "Basic.h"
#include "BasicMathGenerated.h"

#include <immintrin.h>


inline u32 FirstBitLow32(u32 mask)  { return _tzcnt_u32(mask); }
inline u32 FirstBitHigh32(u32 mask) { return 31 - _lzcnt_u32(mask); }
inline u32 CountSetBits32(u32 mask) { return _mm_popcnt_u32(mask); }
inline u32 CountLeadingZeros32(u32 mask) { return _lzcnt_u32(mask); }
inline bool IsPowerOfTwo32(u32 value) { return CountSetBits32(value) == 1; }
inline u32 RoundUpToPowerOfTwo32(u32 value) { return 1u << (32 - CountLeadingZeros32(value - 1)); }

inline u64 FirstBitLow(u64 mask)  { return _tzcnt_u64(mask); }
inline u64 FirstBitHigh(u64 mask) { return 63 - _lzcnt_u64(mask); }
inline u64 CountSetBits(u64 mask) { return _mm_popcnt_u64(mask); }
inline u64 CountLeadingZeros(u64 mask) { return _lzcnt_u64(mask); }
inline bool IsPowerOfTwo(u64 value) { return CountSetBits(value) == 1; }
inline u64 RoundUpToPowerOfTwo(u64 value) { return 1llu << (64 - CountLeadingZeros(value - 1)); }

inline u64 AlignUp(u64 size, u64 alignment) { DebugAssert(IsPowerOfTwo(alignment), "Invalid alignment '0x%llX'. Alignment must be a power of 2.", alignment); return (size + alignment - 1) & ~(alignment - 1); }
inline u32 AlignUp(u32 size, u32 alignment) { DebugAssert(IsPowerOfTwo32(alignment), "Invalid alignment '0x%llX'. Alignment must be a power of 2.", alignment); return (size + alignment - 1) & ~(alignment - 1); }
inline u64 RoundUp(u64 size, u64 alignment) { return ((size + alignment - 1) / alignment) * alignment; }
inline u32 RoundUp(u32 size, u32 alignment) { return ((size + alignment - 1) / alignment) * alignment; }

inline u64 Min(u64 lh, u64 rh) { return lh < rh ? lh : rh; }
inline u64 Max(u64 lh, u64 rh) { return lh > rh ? lh : rh; }
inline u32 Min(u32 lh, u32 rh) { return lh < rh ? lh : rh; }
inline u32 Max(u32 lh, u32 rh) { return lh > rh ? lh : rh; }
inline u16 Min(u16 lh, u16 rh) { return lh < rh ? lh : rh; }
inline u16 Max(u16 lh, u16 rh) { return lh > rh ? lh : rh; }
inline u8  Min(u8  lh, u8  rh) { return lh < rh ? lh : rh; }
inline u8  Max(u8  lh, u8  rh) { return lh > rh ? lh : rh; }
inline float Max(float lh, float rh) { return lh > rh ? lh : rh; }

template<typename T, T(FirstBitLowT)(T)>
struct BitScanLowT {
	explicit BitScanLowT(T mask) : mask(mask) {}
	T mask = 0;
	
	struct Iterator {
		T mask = 0;
		
		Iterator& operator++ () { mask &= (mask - 1); return *this; }
		bool operator!= (const Iterator&) const { return mask != 0; }
		T operator* () { return FirstBitLowT(mask); };
	};
	
	Iterator begin() const { return Iterator{ mask }; }
	Iterator end()   const { return {}; }
};
using BitScanLow   = BitScanLowT<u64, FirstBitLow>;
using BitScanLow32 = BitScanLowT<u32, FirstBitLow32>;


using float4 = Math::Vec4f;
using float3 = Math::Vec3f;
using float2 = Math::Vec2f;

using uint4 = Math::Vec4u32;
using uint3 = Math::Vec3u32;
using uint2 = Math::Vec2u32;

using float4x4 = Math::Mat4x4f;
using float3x4 = Math::Mat3x4f;
using float3x3 = Math::Mat3x3f;


inline u32   DivideAndRoundUp(u32   numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }
inline uint2 DivideAndRoundUp(uint2 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }
inline uint3 DivideAndRoundUp(uint3 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }
inline uint4 DivideAndRoundUp(uint4 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }

namespace Math {
	compile_const float PI  = 3.1415927f;
	compile_const float TAU = 6.2831854f;
	compile_const float degrees_to_radians = 0.017453292f;
	compile_const float radians_to_degress = 57.29578f;
	
	bool IsPerspectiveMatrix(const float4& coefficients);
	bool IsOrthographicMatrix(const float4& coefficients);
	float4 PerspectiveViewToClip(float vertical_fov, float2 viewport_size, float near_depth);
	float4 OrthographicViewToClip(float2 size, float far_depth);
	float4 ViewToClipInverse(const float4& view_to_clip_coef);
}
