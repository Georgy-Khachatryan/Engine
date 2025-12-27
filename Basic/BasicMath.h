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
inline u32 DepositBits32(u32 value, u32 mask) { return _pdep_u32(value, mask); }
inline u32 ExtractBits32(u32 value, u32 mask) { return _pext_u32(value, mask); }

inline u64 FirstBitLow(u64 mask)  { return _tzcnt_u64(mask); }
inline u64 FirstBitHigh(u64 mask) { return 63 - _lzcnt_u64(mask); }
inline u64 CountSetBits(u64 mask) { return _mm_popcnt_u64(mask); }
inline u64 CountLeadingZeros(u64 mask) { return _lzcnt_u64(mask); }
inline bool IsPowerOfTwo(u64 value) { return CountSetBits(value) == 1; }
inline u64 RoundUpToPowerOfTwo(u64 value) { return 1llu << (64 - CountLeadingZeros(value - 1)); }
inline u64 DepositBits(u64 value, u64 mask) { return _pdep_u64(value, mask); }
inline u64 ExtractBits(u64 value, u64 mask) { return _pext_u64(value, mask); }

inline u64 AlignUp(u64 size, u64 alignment) { DebugAssert(IsPowerOfTwo(alignment), "Invalid alignment '0x%x'. Alignment must be a power of 2.", alignment); return (size + alignment - 1) & ~(alignment - 1); }
inline u32 AlignUp(u32 size, u32 alignment) { DebugAssert(IsPowerOfTwo32(alignment), "Invalid alignment '0x%x'. Alignment must be a power of 2.", alignment); return (size + alignment - 1) & ~(alignment - 1); }
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

namespace Math {
	struct Quatf {
		union {
			struct { float x; float y; float z; float w; };
			struct { Vec2f xy; Vec2f zw; };
			Vec3f xyz;
		};
		
		constexpr Quatf() : x(0.f), y(0.f), z(0.f), w(1.f) {}
		constexpr Quatf(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		constexpr Quatf(const Vec3f& xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Quatf(const Quatf& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}
		
		template<typename T> explicit constexpr Quatf(const T& xyzw) : x((float)xyzw.x), y((float)xyzw.y), z((float)xyzw.z), w((float)xyzw.w) {}
		
		friend Quatf operator*(const Quatf& lh, const Quatf& rh) {
			Quatf result;
			result.x = lh.w * rh.x + lh.x * rh.w + lh.y * rh.z - lh.z * rh.y;
			result.y = lh.w * rh.y - lh.x * rh.z + lh.y * rh.w + lh.z * rh.x;
			result.z = lh.w * rh.z + lh.x * rh.y - lh.y * rh.x + lh.z * rh.w;
			result.w = lh.w * rh.w - lh.x * rh.x - lh.y * rh.y - lh.z * rh.z;
			return result;
		}
		
		friend Vec3f operator*(const Quatf& q, const Vec3f& v) {
			// Rotate a vector with a quaternion. Equivalent to QuatToRotationMatrix(q) * v
			// https://fgiesen.wordpress.com/2019/02/09/rotating-a-single-vector-using-a-quaternion/
			auto t = Math::Cross(q.xyz, v) * 2.f;
			return v + t * q.w + Math::Cross(q.xyz, t);
		}
		
		Quatf operator*(float other) const { return Quatf(x * other, y * other, z * other, w * other); }
		Quatf operator/(float other) const { return Quatf(x / other, y / other, z / other, w / other); }
		
		Quatf& operator*=(const Quatf& other) { *this = *this * other; return *this; }
		Quatf& operator*=(float other) { x *= other; y *= other; z *= other; w *= other; return *this; }
		Quatf& operator/=(float other) { x /= other; y /= other; z /= other; w /= other; return *this; }
		
		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }
		
		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = float;
	};
	
	inline float Dot(const Quatf& lh, const Quatf& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z + lh.w * rh.w; }
	inline float LengthSquare(const Quatf& v) { return Dot(v, v); }
	inline float Length(const Quatf& v) { return sqrtf(Dot(v, v)); }
	inline Quatf Normalize(const Quatf& v) { return v * (1.f / Length(v)); }
	inline Quatf Conjugate(const Quatf& v) { return Quatf(-v.x, -v.y, -v.z, v.w); }
}

using quat = Math::Quatf;

using float4 = Math::Vec4f;
using float3 = Math::Vec3f;
using float2 = Math::Vec2f;

using uint4 = Math::Vec4u32;
using uint3 = Math::Vec3u32;
using uint2 = Math::Vec2u32;

using float4x4 = Math::Mat4x4f;
using float3x4 = Math::Mat3x4f;
using float3x3 = Math::Mat3x3f;


compile_const u32 DivideAndRoundUp(u32 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }
compile_const u64 DivideAndRoundUp(u64 numerator, u64 denominator) { return (numerator + (denominator - 1)) / denominator; }
inline uint2 DivideAndRoundUp(uint2 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }
inline uint3 DivideAndRoundUp(uint3 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }
inline uint4 DivideAndRoundUp(uint4 numerator, u32 denominator) { return (numerator + (denominator - 1)) / denominator; }


namespace Math {
	compile_const float PI  = 3.1415927f;
	compile_const float TAU = 6.2831854f;
	compile_const float degrees_to_radians = 0.017453292f;
	compile_const float radians_to_degress = 57.29578f;
	
	struct RayInfo {
		float3 origin;
		float3 direction;
	};
	
	bool IsPerspectiveMatrix(const float4& coefficients);
	bool IsOrthographicMatrix(const float4& coefficients);
	float4 PerspectiveViewToClip(float vertical_fov, float2 viewport_size, float near_depth);
	float4 OrthographicViewToClip(float2 size, float far_depth);
	float4 ViewToClipInverse(const float4& view_to_clip_coef);
	
	inline float2 NdcToScreenUv(const float2& ndc) { return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f); }
	inline float2 ScreenUvToNdc(const float2& uv)  { return float2(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f); }
	
	RayInfo RayInfoFromNdc(float2 ndc, const float4& clip_to_view_coef);
	RayInfo RayInfoFromScreenUv(float2 uv, const float4& clip_to_view_coef);
	
	quat AxisAngleToQuat(const float3& axis, float angle);
	float3x3 QuatToRotationMatrix(const quat& q);
}
