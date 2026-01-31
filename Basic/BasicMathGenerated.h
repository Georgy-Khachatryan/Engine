#pragma once
#include "Basic.h"

#include <math.h>

namespace Math {

	inline u64 Min(u64 lh, u64 rh) { return lh < rh ? lh : rh; }
	inline u64 Max(u64 lh, u64 rh) { return lh > rh ? lh : rh; }
	inline u32 Min(u32 lh, u32 rh) { return lh < rh ? lh : rh; }
	inline u32 Max(u32 lh, u32 rh) { return lh > rh ? lh : rh; }
	inline u16 Min(u16 lh, u16 rh) { return lh < rh ? lh : rh; }
	inline u16 Max(u16 lh, u16 rh) { return lh > rh ? lh : rh; }
	inline u8  Min(u8  lh, u8  rh) { return lh < rh ? lh : rh; }
	inline u8  Max(u8  lh, u8  rh) { return lh > rh ? lh : rh; }

	inline s64 Min(s64 lh, s64 rh) { return lh < rh ? lh : rh; }
	inline s64 Max(s64 lh, s64 rh) { return lh > rh ? lh : rh; }
	inline s32 Min(s32 lh, s32 rh) { return lh < rh ? lh : rh; }
	inline s32 Max(s32 lh, s32 rh) { return lh > rh ? lh : rh; }
	inline s16 Min(s16 lh, s16 rh) { return lh < rh ? lh : rh; }
	inline s16 Max(s16 lh, s16 rh) { return lh > rh ? lh : rh; }
	inline s8  Min(s8  lh, s8  rh) { return lh < rh ? lh : rh; }
	inline s8  Max(s8  lh, s8  rh) { return lh > rh ? lh : rh; }

	inline float Min(float lh, float rh) { return lh < rh ? lh : rh; }
	inline float Max(float lh, float rh) { return lh > rh ? lh : rh; }

	struct Vec2u32 {
		u32 x; u32 y;

		constexpr Vec2u32() : x(0), y(0) {}
		constexpr Vec2u32(u32 x) : x(x), y(x) {}
		constexpr Vec2u32(u32 x, u32 y) : x(x), y(y) {}
		constexpr Vec2u32(const Vec2u32& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2u32(const T& xy) : x((u32)xy.x), y((u32)xy.y) {}

		Vec2u32 operator+(const Vec2u32& other) const { return Vec2u32(x + other.x, y + other.y); }
		Vec2u32 operator+(u32 other) const { return Vec2u32(x + other, y + other); }

		Vec2u32 operator-(const Vec2u32& other) const { return Vec2u32(x - other.x, y - other.y); }
		Vec2u32 operator-(u32 other) const { return Vec2u32(x - other, y - other); }

		Vec2u32 operator*(const Vec2u32& other) const { return Vec2u32(x * other.x, y * other.y); }
		Vec2u32 operator*(u32 other) const { return Vec2u32(x * other, y * other); }

		Vec2u32 operator/(const Vec2u32& other) const { return Vec2u32(x / other.x, y / other.y); }
		Vec2u32 operator/(u32 other) const { return Vec2u32(x / other, y / other); }

		Vec2u32 operator%(const Vec2u32& other) const { return Vec2u32(x % other.x, y % other.y); }
		Vec2u32 operator%(u32 other) const { return Vec2u32(x % other, y % other); }

		Vec2u32 operator&(const Vec2u32& other) const { return Vec2u32(x & other.x, y & other.y); }
		Vec2u32 operator&(u32 other) const { return Vec2u32(x & other, y & other); }

		Vec2u32 operator|(const Vec2u32& other) const { return Vec2u32(x | other.x, y | other.y); }
		Vec2u32 operator|(u32 other) const { return Vec2u32(x | other, y | other); }

		Vec2u32 operator^(const Vec2u32& other) const { return Vec2u32(x ^ other.x, y ^ other.y); }
		Vec2u32 operator^(u32 other) const { return Vec2u32(x ^ other, y ^ other); }

		Vec2u32 operator<<(const Vec2u32& other) const { return Vec2u32(x << other.x, y << other.y); }
		Vec2u32 operator<<(u32 other) const { return Vec2u32(x << other, y << other); }

		Vec2u32 operator>>(const Vec2u32& other) const { return Vec2u32(x >> other.x, y >> other.y); }
		Vec2u32 operator>>(u32 other) const { return Vec2u32(x >> other, y >> other); }

		Vec2u32& operator+=(const Vec2u32& other) { x += other.x; y += other.y; return *this; }
		Vec2u32& operator+=(u32 other) { x += other; y += other; return *this; }

		Vec2u32& operator-=(const Vec2u32& other) { x -= other.x; y -= other.y; return *this; }
		Vec2u32& operator-=(u32 other) { x -= other; y -= other; return *this; }

		Vec2u32& operator*=(const Vec2u32& other) { x *= other.x; y *= other.y; return *this; }
		Vec2u32& operator*=(u32 other) { x *= other; y *= other; return *this; }

		Vec2u32& operator/=(const Vec2u32& other) { x /= other.x; y /= other.y; return *this; }
		Vec2u32& operator/=(u32 other) { x /= other; y /= other; return *this; }

		Vec2u32& operator%=(const Vec2u32& other) { x %= other.x; y %= other.y; return *this; }
		Vec2u32& operator%=(u32 other) { x %= other; y %= other; return *this; }

		Vec2u32& operator&=(const Vec2u32& other) { x &= other.x; y &= other.y; return *this; }
		Vec2u32& operator&=(u32 other) { x &= other; y &= other; return *this; }

		Vec2u32& operator|=(const Vec2u32& other) { x |= other.x; y |= other.y; return *this; }
		Vec2u32& operator|=(u32 other) { x |= other; y |= other; return *this; }

		Vec2u32& operator^=(const Vec2u32& other) { x ^= other.x; y ^= other.y; return *this; }
		Vec2u32& operator^=(u32 other) { x ^= other; y ^= other; return *this; }

		Vec2u32& operator<<=(const Vec2u32& other) { x <<= other.x; y <<= other.y; return *this; }
		Vec2u32& operator<<=(u32 other) { x <<= other; y <<= other; return *this; }

		Vec2u32& operator>>=(const Vec2u32& other) { x >>= other.x; y >>= other.y; return *this; }
		Vec2u32& operator>>=(u32 other) { x >>= other; y >>= other; return *this; }

		Vec2u32 operator~() const { return Vec2u32(~x, ~y); }

		u32& operator[](u32 index) { return (&x)[index]; }
		const u32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = u32;
	};

	inline Vec2u32 Min(const Vec2u32& lh, const Vec2u32& rh) { return Vec2u32(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2u32 Max(const Vec2u32& lh, const Vec2u32& rh) { return Vec2u32(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec2u16 {
		u16 x; u16 y;

		constexpr Vec2u16() : x(0), y(0) {}
		constexpr Vec2u16(u16 x) : x(x), y(x) {}
		constexpr Vec2u16(u16 x, u16 y) : x(x), y(y) {}
		constexpr Vec2u16(const Vec2u16& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2u16(const T& xy) : x((u16)xy.x), y((u16)xy.y) {}

		Vec2u16 operator+(const Vec2u16& other) const { return Vec2u16(x + other.x, y + other.y); }
		Vec2u16 operator+(u16 other) const { return Vec2u16(x + other, y + other); }

		Vec2u16 operator-(const Vec2u16& other) const { return Vec2u16(x - other.x, y - other.y); }
		Vec2u16 operator-(u16 other) const { return Vec2u16(x - other, y - other); }

		Vec2u16 operator*(const Vec2u16& other) const { return Vec2u16(x * other.x, y * other.y); }
		Vec2u16 operator*(u16 other) const { return Vec2u16(x * other, y * other); }

		Vec2u16 operator/(const Vec2u16& other) const { return Vec2u16(x / other.x, y / other.y); }
		Vec2u16 operator/(u16 other) const { return Vec2u16(x / other, y / other); }

		Vec2u16 operator%(const Vec2u16& other) const { return Vec2u16(x % other.x, y % other.y); }
		Vec2u16 operator%(u16 other) const { return Vec2u16(x % other, y % other); }

		Vec2u16 operator&(const Vec2u16& other) const { return Vec2u16(x & other.x, y & other.y); }
		Vec2u16 operator&(u16 other) const { return Vec2u16(x & other, y & other); }

		Vec2u16 operator|(const Vec2u16& other) const { return Vec2u16(x | other.x, y | other.y); }
		Vec2u16 operator|(u16 other) const { return Vec2u16(x | other, y | other); }

		Vec2u16 operator^(const Vec2u16& other) const { return Vec2u16(x ^ other.x, y ^ other.y); }
		Vec2u16 operator^(u16 other) const { return Vec2u16(x ^ other, y ^ other); }

		Vec2u16 operator<<(const Vec2u16& other) const { return Vec2u16(x << other.x, y << other.y); }
		Vec2u16 operator<<(u16 other) const { return Vec2u16(x << other, y << other); }

		Vec2u16 operator>>(const Vec2u16& other) const { return Vec2u16(x >> other.x, y >> other.y); }
		Vec2u16 operator>>(u16 other) const { return Vec2u16(x >> other, y >> other); }

		Vec2u16& operator+=(const Vec2u16& other) { x += other.x; y += other.y; return *this; }
		Vec2u16& operator+=(u16 other) { x += other; y += other; return *this; }

		Vec2u16& operator-=(const Vec2u16& other) { x -= other.x; y -= other.y; return *this; }
		Vec2u16& operator-=(u16 other) { x -= other; y -= other; return *this; }

		Vec2u16& operator*=(const Vec2u16& other) { x *= other.x; y *= other.y; return *this; }
		Vec2u16& operator*=(u16 other) { x *= other; y *= other; return *this; }

		Vec2u16& operator/=(const Vec2u16& other) { x /= other.x; y /= other.y; return *this; }
		Vec2u16& operator/=(u16 other) { x /= other; y /= other; return *this; }

		Vec2u16& operator%=(const Vec2u16& other) { x %= other.x; y %= other.y; return *this; }
		Vec2u16& operator%=(u16 other) { x %= other; y %= other; return *this; }

		Vec2u16& operator&=(const Vec2u16& other) { x &= other.x; y &= other.y; return *this; }
		Vec2u16& operator&=(u16 other) { x &= other; y &= other; return *this; }

		Vec2u16& operator|=(const Vec2u16& other) { x |= other.x; y |= other.y; return *this; }
		Vec2u16& operator|=(u16 other) { x |= other; y |= other; return *this; }

		Vec2u16& operator^=(const Vec2u16& other) { x ^= other.x; y ^= other.y; return *this; }
		Vec2u16& operator^=(u16 other) { x ^= other; y ^= other; return *this; }

		Vec2u16& operator<<=(const Vec2u16& other) { x <<= other.x; y <<= other.y; return *this; }
		Vec2u16& operator<<=(u16 other) { x <<= other; y <<= other; return *this; }

		Vec2u16& operator>>=(const Vec2u16& other) { x >>= other.x; y >>= other.y; return *this; }
		Vec2u16& operator>>=(u16 other) { x >>= other; y >>= other; return *this; }

		Vec2u16 operator~() const { return Vec2u16(~x, ~y); }

		u16& operator[](u32 index) { return (&x)[index]; }
		const u16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = u16;
	};

	inline Vec2u16 Min(const Vec2u16& lh, const Vec2u16& rh) { return Vec2u16(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2u16 Max(const Vec2u16& lh, const Vec2u16& rh) { return Vec2u16(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec2u8 {
		u8 x; u8 y;

		constexpr Vec2u8() : x(0), y(0) {}
		constexpr Vec2u8(u8 x) : x(x), y(x) {}
		constexpr Vec2u8(u8 x, u8 y) : x(x), y(y) {}
		constexpr Vec2u8(const Vec2u8& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2u8(const T& xy) : x((u8)xy.x), y((u8)xy.y) {}

		Vec2u8 operator+(const Vec2u8& other) const { return Vec2u8(x + other.x, y + other.y); }
		Vec2u8 operator+(u8 other) const { return Vec2u8(x + other, y + other); }

		Vec2u8 operator-(const Vec2u8& other) const { return Vec2u8(x - other.x, y - other.y); }
		Vec2u8 operator-(u8 other) const { return Vec2u8(x - other, y - other); }

		Vec2u8 operator*(const Vec2u8& other) const { return Vec2u8(x * other.x, y * other.y); }
		Vec2u8 operator*(u8 other) const { return Vec2u8(x * other, y * other); }

		Vec2u8 operator/(const Vec2u8& other) const { return Vec2u8(x / other.x, y / other.y); }
		Vec2u8 operator/(u8 other) const { return Vec2u8(x / other, y / other); }

		Vec2u8 operator%(const Vec2u8& other) const { return Vec2u8(x % other.x, y % other.y); }
		Vec2u8 operator%(u8 other) const { return Vec2u8(x % other, y % other); }

		Vec2u8 operator&(const Vec2u8& other) const { return Vec2u8(x & other.x, y & other.y); }
		Vec2u8 operator&(u8 other) const { return Vec2u8(x & other, y & other); }

		Vec2u8 operator|(const Vec2u8& other) const { return Vec2u8(x | other.x, y | other.y); }
		Vec2u8 operator|(u8 other) const { return Vec2u8(x | other, y | other); }

		Vec2u8 operator^(const Vec2u8& other) const { return Vec2u8(x ^ other.x, y ^ other.y); }
		Vec2u8 operator^(u8 other) const { return Vec2u8(x ^ other, y ^ other); }

		Vec2u8 operator<<(const Vec2u8& other) const { return Vec2u8(x << other.x, y << other.y); }
		Vec2u8 operator<<(u8 other) const { return Vec2u8(x << other, y << other); }

		Vec2u8 operator>>(const Vec2u8& other) const { return Vec2u8(x >> other.x, y >> other.y); }
		Vec2u8 operator>>(u8 other) const { return Vec2u8(x >> other, y >> other); }

		Vec2u8& operator+=(const Vec2u8& other) { x += other.x; y += other.y; return *this; }
		Vec2u8& operator+=(u8 other) { x += other; y += other; return *this; }

		Vec2u8& operator-=(const Vec2u8& other) { x -= other.x; y -= other.y; return *this; }
		Vec2u8& operator-=(u8 other) { x -= other; y -= other; return *this; }

		Vec2u8& operator*=(const Vec2u8& other) { x *= other.x; y *= other.y; return *this; }
		Vec2u8& operator*=(u8 other) { x *= other; y *= other; return *this; }

		Vec2u8& operator/=(const Vec2u8& other) { x /= other.x; y /= other.y; return *this; }
		Vec2u8& operator/=(u8 other) { x /= other; y /= other; return *this; }

		Vec2u8& operator%=(const Vec2u8& other) { x %= other.x; y %= other.y; return *this; }
		Vec2u8& operator%=(u8 other) { x %= other; y %= other; return *this; }

		Vec2u8& operator&=(const Vec2u8& other) { x &= other.x; y &= other.y; return *this; }
		Vec2u8& operator&=(u8 other) { x &= other; y &= other; return *this; }

		Vec2u8& operator|=(const Vec2u8& other) { x |= other.x; y |= other.y; return *this; }
		Vec2u8& operator|=(u8 other) { x |= other; y |= other; return *this; }

		Vec2u8& operator^=(const Vec2u8& other) { x ^= other.x; y ^= other.y; return *this; }
		Vec2u8& operator^=(u8 other) { x ^= other; y ^= other; return *this; }

		Vec2u8& operator<<=(const Vec2u8& other) { x <<= other.x; y <<= other.y; return *this; }
		Vec2u8& operator<<=(u8 other) { x <<= other; y <<= other; return *this; }

		Vec2u8& operator>>=(const Vec2u8& other) { x >>= other.x; y >>= other.y; return *this; }
		Vec2u8& operator>>=(u8 other) { x >>= other; y >>= other; return *this; }

		Vec2u8 operator~() const { return Vec2u8(~x, ~y); }

		u8& operator[](u32 index) { return (&x)[index]; }
		const u8& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = u8;
	};

	inline Vec2u8 Min(const Vec2u8& lh, const Vec2u8& rh) { return Vec2u8(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2u8 Max(const Vec2u8& lh, const Vec2u8& rh) { return Vec2u8(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec3u32 {
		union {
			struct { u32 x; u32 y; u32 z; };
			Vec2u32 xy;
		};

		constexpr Vec3u32() : x(0), y(0), z(0) {}
		constexpr Vec3u32(u32 x) : x(x), y(x), z(x) {}
		constexpr Vec3u32(u32 x, u32 y, u32 z) : x(x), y(y), z(z) {}
		constexpr Vec3u32(const Vec2u32& xy, u32 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3u32(const Vec3u32& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3u32(const T& xyz) : x((u32)xyz.x), y((u32)xyz.y), z((u32)xyz.z) {}

		Vec3u32 operator+(const Vec3u32& other) const { return Vec3u32(x + other.x, y + other.y, z + other.z); }
		Vec3u32 operator+(u32 other) const { return Vec3u32(x + other, y + other, z + other); }

		Vec3u32 operator-(const Vec3u32& other) const { return Vec3u32(x - other.x, y - other.y, z - other.z); }
		Vec3u32 operator-(u32 other) const { return Vec3u32(x - other, y - other, z - other); }

		Vec3u32 operator*(const Vec3u32& other) const { return Vec3u32(x * other.x, y * other.y, z * other.z); }
		Vec3u32 operator*(u32 other) const { return Vec3u32(x * other, y * other, z * other); }

		Vec3u32 operator/(const Vec3u32& other) const { return Vec3u32(x / other.x, y / other.y, z / other.z); }
		Vec3u32 operator/(u32 other) const { return Vec3u32(x / other, y / other, z / other); }

		Vec3u32 operator%(const Vec3u32& other) const { return Vec3u32(x % other.x, y % other.y, z % other.z); }
		Vec3u32 operator%(u32 other) const { return Vec3u32(x % other, y % other, z % other); }

		Vec3u32 operator&(const Vec3u32& other) const { return Vec3u32(x & other.x, y & other.y, z & other.z); }
		Vec3u32 operator&(u32 other) const { return Vec3u32(x & other, y & other, z & other); }

		Vec3u32 operator|(const Vec3u32& other) const { return Vec3u32(x | other.x, y | other.y, z | other.z); }
		Vec3u32 operator|(u32 other) const { return Vec3u32(x | other, y | other, z | other); }

		Vec3u32 operator^(const Vec3u32& other) const { return Vec3u32(x ^ other.x, y ^ other.y, z ^ other.z); }
		Vec3u32 operator^(u32 other) const { return Vec3u32(x ^ other, y ^ other, z ^ other); }

		Vec3u32 operator<<(const Vec3u32& other) const { return Vec3u32(x << other.x, y << other.y, z << other.z); }
		Vec3u32 operator<<(u32 other) const { return Vec3u32(x << other, y << other, z << other); }

		Vec3u32 operator>>(const Vec3u32& other) const { return Vec3u32(x >> other.x, y >> other.y, z >> other.z); }
		Vec3u32 operator>>(u32 other) const { return Vec3u32(x >> other, y >> other, z >> other); }

		Vec3u32& operator+=(const Vec3u32& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3u32& operator+=(u32 other) { x += other; y += other; z += other; return *this; }

		Vec3u32& operator-=(const Vec3u32& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3u32& operator-=(u32 other) { x -= other; y -= other; z -= other; return *this; }

		Vec3u32& operator*=(const Vec3u32& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3u32& operator*=(u32 other) { x *= other; y *= other; z *= other; return *this; }

		Vec3u32& operator/=(const Vec3u32& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3u32& operator/=(u32 other) { x /= other; y /= other; z /= other; return *this; }

		Vec3u32& operator%=(const Vec3u32& other) { x %= other.x; y %= other.y; z %= other.z; return *this; }
		Vec3u32& operator%=(u32 other) { x %= other; y %= other; z %= other; return *this; }

		Vec3u32& operator&=(const Vec3u32& other) { x &= other.x; y &= other.y; z &= other.z; return *this; }
		Vec3u32& operator&=(u32 other) { x &= other; y &= other; z &= other; return *this; }

		Vec3u32& operator|=(const Vec3u32& other) { x |= other.x; y |= other.y; z |= other.z; return *this; }
		Vec3u32& operator|=(u32 other) { x |= other; y |= other; z |= other; return *this; }

		Vec3u32& operator^=(const Vec3u32& other) { x ^= other.x; y ^= other.y; z ^= other.z; return *this; }
		Vec3u32& operator^=(u32 other) { x ^= other; y ^= other; z ^= other; return *this; }

		Vec3u32& operator<<=(const Vec3u32& other) { x <<= other.x; y <<= other.y; z <<= other.z; return *this; }
		Vec3u32& operator<<=(u32 other) { x <<= other; y <<= other; z <<= other; return *this; }

		Vec3u32& operator>>=(const Vec3u32& other) { x >>= other.x; y >>= other.y; z >>= other.z; return *this; }
		Vec3u32& operator>>=(u32 other) { x >>= other; y >>= other; z >>= other; return *this; }

		Vec3u32 operator~() const { return Vec3u32(~x, ~y, ~z); }

		u32& operator[](u32 index) { return (&x)[index]; }
		const u32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = u32;
	};

	inline Vec3u32 Min(const Vec3u32& lh, const Vec3u32& rh) { return Vec3u32(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3u32 Max(const Vec3u32& lh, const Vec3u32& rh) { return Vec3u32(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec3u16 {
		union {
			struct { u16 x; u16 y; u16 z; };
			Vec2u16 xy;
		};

		constexpr Vec3u16() : x(0), y(0), z(0) {}
		constexpr Vec3u16(u16 x) : x(x), y(x), z(x) {}
		constexpr Vec3u16(u16 x, u16 y, u16 z) : x(x), y(y), z(z) {}
		constexpr Vec3u16(const Vec2u16& xy, u16 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3u16(const Vec3u16& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3u16(const T& xyz) : x((u16)xyz.x), y((u16)xyz.y), z((u16)xyz.z) {}

		Vec3u16 operator+(const Vec3u16& other) const { return Vec3u16(x + other.x, y + other.y, z + other.z); }
		Vec3u16 operator+(u16 other) const { return Vec3u16(x + other, y + other, z + other); }

		Vec3u16 operator-(const Vec3u16& other) const { return Vec3u16(x - other.x, y - other.y, z - other.z); }
		Vec3u16 operator-(u16 other) const { return Vec3u16(x - other, y - other, z - other); }

		Vec3u16 operator*(const Vec3u16& other) const { return Vec3u16(x * other.x, y * other.y, z * other.z); }
		Vec3u16 operator*(u16 other) const { return Vec3u16(x * other, y * other, z * other); }

		Vec3u16 operator/(const Vec3u16& other) const { return Vec3u16(x / other.x, y / other.y, z / other.z); }
		Vec3u16 operator/(u16 other) const { return Vec3u16(x / other, y / other, z / other); }

		Vec3u16 operator%(const Vec3u16& other) const { return Vec3u16(x % other.x, y % other.y, z % other.z); }
		Vec3u16 operator%(u16 other) const { return Vec3u16(x % other, y % other, z % other); }

		Vec3u16 operator&(const Vec3u16& other) const { return Vec3u16(x & other.x, y & other.y, z & other.z); }
		Vec3u16 operator&(u16 other) const { return Vec3u16(x & other, y & other, z & other); }

		Vec3u16 operator|(const Vec3u16& other) const { return Vec3u16(x | other.x, y | other.y, z | other.z); }
		Vec3u16 operator|(u16 other) const { return Vec3u16(x | other, y | other, z | other); }

		Vec3u16 operator^(const Vec3u16& other) const { return Vec3u16(x ^ other.x, y ^ other.y, z ^ other.z); }
		Vec3u16 operator^(u16 other) const { return Vec3u16(x ^ other, y ^ other, z ^ other); }

		Vec3u16 operator<<(const Vec3u16& other) const { return Vec3u16(x << other.x, y << other.y, z << other.z); }
		Vec3u16 operator<<(u16 other) const { return Vec3u16(x << other, y << other, z << other); }

		Vec3u16 operator>>(const Vec3u16& other) const { return Vec3u16(x >> other.x, y >> other.y, z >> other.z); }
		Vec3u16 operator>>(u16 other) const { return Vec3u16(x >> other, y >> other, z >> other); }

		Vec3u16& operator+=(const Vec3u16& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3u16& operator+=(u16 other) { x += other; y += other; z += other; return *this; }

		Vec3u16& operator-=(const Vec3u16& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3u16& operator-=(u16 other) { x -= other; y -= other; z -= other; return *this; }

		Vec3u16& operator*=(const Vec3u16& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3u16& operator*=(u16 other) { x *= other; y *= other; z *= other; return *this; }

		Vec3u16& operator/=(const Vec3u16& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3u16& operator/=(u16 other) { x /= other; y /= other; z /= other; return *this; }

		Vec3u16& operator%=(const Vec3u16& other) { x %= other.x; y %= other.y; z %= other.z; return *this; }
		Vec3u16& operator%=(u16 other) { x %= other; y %= other; z %= other; return *this; }

		Vec3u16& operator&=(const Vec3u16& other) { x &= other.x; y &= other.y; z &= other.z; return *this; }
		Vec3u16& operator&=(u16 other) { x &= other; y &= other; z &= other; return *this; }

		Vec3u16& operator|=(const Vec3u16& other) { x |= other.x; y |= other.y; z |= other.z; return *this; }
		Vec3u16& operator|=(u16 other) { x |= other; y |= other; z |= other; return *this; }

		Vec3u16& operator^=(const Vec3u16& other) { x ^= other.x; y ^= other.y; z ^= other.z; return *this; }
		Vec3u16& operator^=(u16 other) { x ^= other; y ^= other; z ^= other; return *this; }

		Vec3u16& operator<<=(const Vec3u16& other) { x <<= other.x; y <<= other.y; z <<= other.z; return *this; }
		Vec3u16& operator<<=(u16 other) { x <<= other; y <<= other; z <<= other; return *this; }

		Vec3u16& operator>>=(const Vec3u16& other) { x >>= other.x; y >>= other.y; z >>= other.z; return *this; }
		Vec3u16& operator>>=(u16 other) { x >>= other; y >>= other; z >>= other; return *this; }

		Vec3u16 operator~() const { return Vec3u16(~x, ~y, ~z); }

		u16& operator[](u32 index) { return (&x)[index]; }
		const u16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = u16;
	};

	inline Vec3u16 Min(const Vec3u16& lh, const Vec3u16& rh) { return Vec3u16(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3u16 Max(const Vec3u16& lh, const Vec3u16& rh) { return Vec3u16(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec3u8 {
		union {
			struct { u8 x; u8 y; u8 z; };
			Vec2u8 xy;
		};

		constexpr Vec3u8() : x(0), y(0), z(0) {}
		constexpr Vec3u8(u8 x) : x(x), y(x), z(x) {}
		constexpr Vec3u8(u8 x, u8 y, u8 z) : x(x), y(y), z(z) {}
		constexpr Vec3u8(const Vec2u8& xy, u8 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3u8(const Vec3u8& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3u8(const T& xyz) : x((u8)xyz.x), y((u8)xyz.y), z((u8)xyz.z) {}

		Vec3u8 operator+(const Vec3u8& other) const { return Vec3u8(x + other.x, y + other.y, z + other.z); }
		Vec3u8 operator+(u8 other) const { return Vec3u8(x + other, y + other, z + other); }

		Vec3u8 operator-(const Vec3u8& other) const { return Vec3u8(x - other.x, y - other.y, z - other.z); }
		Vec3u8 operator-(u8 other) const { return Vec3u8(x - other, y - other, z - other); }

		Vec3u8 operator*(const Vec3u8& other) const { return Vec3u8(x * other.x, y * other.y, z * other.z); }
		Vec3u8 operator*(u8 other) const { return Vec3u8(x * other, y * other, z * other); }

		Vec3u8 operator/(const Vec3u8& other) const { return Vec3u8(x / other.x, y / other.y, z / other.z); }
		Vec3u8 operator/(u8 other) const { return Vec3u8(x / other, y / other, z / other); }

		Vec3u8 operator%(const Vec3u8& other) const { return Vec3u8(x % other.x, y % other.y, z % other.z); }
		Vec3u8 operator%(u8 other) const { return Vec3u8(x % other, y % other, z % other); }

		Vec3u8 operator&(const Vec3u8& other) const { return Vec3u8(x & other.x, y & other.y, z & other.z); }
		Vec3u8 operator&(u8 other) const { return Vec3u8(x & other, y & other, z & other); }

		Vec3u8 operator|(const Vec3u8& other) const { return Vec3u8(x | other.x, y | other.y, z | other.z); }
		Vec3u8 operator|(u8 other) const { return Vec3u8(x | other, y | other, z | other); }

		Vec3u8 operator^(const Vec3u8& other) const { return Vec3u8(x ^ other.x, y ^ other.y, z ^ other.z); }
		Vec3u8 operator^(u8 other) const { return Vec3u8(x ^ other, y ^ other, z ^ other); }

		Vec3u8 operator<<(const Vec3u8& other) const { return Vec3u8(x << other.x, y << other.y, z << other.z); }
		Vec3u8 operator<<(u8 other) const { return Vec3u8(x << other, y << other, z << other); }

		Vec3u8 operator>>(const Vec3u8& other) const { return Vec3u8(x >> other.x, y >> other.y, z >> other.z); }
		Vec3u8 operator>>(u8 other) const { return Vec3u8(x >> other, y >> other, z >> other); }

		Vec3u8& operator+=(const Vec3u8& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3u8& operator+=(u8 other) { x += other; y += other; z += other; return *this; }

		Vec3u8& operator-=(const Vec3u8& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3u8& operator-=(u8 other) { x -= other; y -= other; z -= other; return *this; }

		Vec3u8& operator*=(const Vec3u8& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3u8& operator*=(u8 other) { x *= other; y *= other; z *= other; return *this; }

		Vec3u8& operator/=(const Vec3u8& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3u8& operator/=(u8 other) { x /= other; y /= other; z /= other; return *this; }

		Vec3u8& operator%=(const Vec3u8& other) { x %= other.x; y %= other.y; z %= other.z; return *this; }
		Vec3u8& operator%=(u8 other) { x %= other; y %= other; z %= other; return *this; }

		Vec3u8& operator&=(const Vec3u8& other) { x &= other.x; y &= other.y; z &= other.z; return *this; }
		Vec3u8& operator&=(u8 other) { x &= other; y &= other; z &= other; return *this; }

		Vec3u8& operator|=(const Vec3u8& other) { x |= other.x; y |= other.y; z |= other.z; return *this; }
		Vec3u8& operator|=(u8 other) { x |= other; y |= other; z |= other; return *this; }

		Vec3u8& operator^=(const Vec3u8& other) { x ^= other.x; y ^= other.y; z ^= other.z; return *this; }
		Vec3u8& operator^=(u8 other) { x ^= other; y ^= other; z ^= other; return *this; }

		Vec3u8& operator<<=(const Vec3u8& other) { x <<= other.x; y <<= other.y; z <<= other.z; return *this; }
		Vec3u8& operator<<=(u8 other) { x <<= other; y <<= other; z <<= other; return *this; }

		Vec3u8& operator>>=(const Vec3u8& other) { x >>= other.x; y >>= other.y; z >>= other.z; return *this; }
		Vec3u8& operator>>=(u8 other) { x >>= other; y >>= other; z >>= other; return *this; }

		Vec3u8 operator~() const { return Vec3u8(~x, ~y, ~z); }

		u8& operator[](u32 index) { return (&x)[index]; }
		const u8& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = u8;
	};

	inline Vec3u8 Min(const Vec3u8& lh, const Vec3u8& rh) { return Vec3u8(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3u8 Max(const Vec3u8& lh, const Vec3u8& rh) { return Vec3u8(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec4u32 {
		union {
			struct { u32 x; u32 y; u32 z; u32 w; };
			struct { Vec2u32 xy; Vec2u32 zw; };
			Vec3u32 xyz;
		};

		constexpr Vec4u32() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4u32(u32 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4u32(u32 x, u32 y, u32 z, u32 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4u32(const Vec2u32& xy, u32 z, u32 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4u32(const Vec2u32& xy, const Vec2u32& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4u32(const Vec3u32& xyz, u32 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4u32(const Vec4u32& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4u32(const T& xyzw) : x((u32)xyzw.x), y((u32)xyzw.y), z((u32)xyzw.z), w((u32)xyzw.w) {}

		Vec4u32 operator+(const Vec4u32& other) const { return Vec4u32(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4u32 operator+(u32 other) const { return Vec4u32(x + other, y + other, z + other, w + other); }

		Vec4u32 operator-(const Vec4u32& other) const { return Vec4u32(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4u32 operator-(u32 other) const { return Vec4u32(x - other, y - other, z - other, w - other); }

		Vec4u32 operator*(const Vec4u32& other) const { return Vec4u32(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4u32 operator*(u32 other) const { return Vec4u32(x * other, y * other, z * other, w * other); }

		Vec4u32 operator/(const Vec4u32& other) const { return Vec4u32(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4u32 operator/(u32 other) const { return Vec4u32(x / other, y / other, z / other, w / other); }

		Vec4u32 operator%(const Vec4u32& other) const { return Vec4u32(x % other.x, y % other.y, z % other.z, w % other.w); }
		Vec4u32 operator%(u32 other) const { return Vec4u32(x % other, y % other, z % other, w % other); }

		Vec4u32 operator&(const Vec4u32& other) const { return Vec4u32(x & other.x, y & other.y, z & other.z, w & other.w); }
		Vec4u32 operator&(u32 other) const { return Vec4u32(x & other, y & other, z & other, w & other); }

		Vec4u32 operator|(const Vec4u32& other) const { return Vec4u32(x | other.x, y | other.y, z | other.z, w | other.w); }
		Vec4u32 operator|(u32 other) const { return Vec4u32(x | other, y | other, z | other, w | other); }

		Vec4u32 operator^(const Vec4u32& other) const { return Vec4u32(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w); }
		Vec4u32 operator^(u32 other) const { return Vec4u32(x ^ other, y ^ other, z ^ other, w ^ other); }

		Vec4u32 operator<<(const Vec4u32& other) const { return Vec4u32(x << other.x, y << other.y, z << other.z, w << other.w); }
		Vec4u32 operator<<(u32 other) const { return Vec4u32(x << other, y << other, z << other, w << other); }

		Vec4u32 operator>>(const Vec4u32& other) const { return Vec4u32(x >> other.x, y >> other.y, z >> other.z, w >> other.w); }
		Vec4u32 operator>>(u32 other) const { return Vec4u32(x >> other, y >> other, z >> other, w >> other); }

		Vec4u32& operator+=(const Vec4u32& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4u32& operator+=(u32 other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4u32& operator-=(const Vec4u32& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4u32& operator-=(u32 other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4u32& operator*=(const Vec4u32& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4u32& operator*=(u32 other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4u32& operator/=(const Vec4u32& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4u32& operator/=(u32 other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4u32& operator%=(const Vec4u32& other) { x %= other.x; y %= other.y; z %= other.z; w %= other.w; return *this; }
		Vec4u32& operator%=(u32 other) { x %= other; y %= other; z %= other; w %= other; return *this; }

		Vec4u32& operator&=(const Vec4u32& other) { x &= other.x; y &= other.y; z &= other.z; w &= other.w; return *this; }
		Vec4u32& operator&=(u32 other) { x &= other; y &= other; z &= other; w &= other; return *this; }

		Vec4u32& operator|=(const Vec4u32& other) { x |= other.x; y |= other.y; z |= other.z; w |= other.w; return *this; }
		Vec4u32& operator|=(u32 other) { x |= other; y |= other; z |= other; w |= other; return *this; }

		Vec4u32& operator^=(const Vec4u32& other) { x ^= other.x; y ^= other.y; z ^= other.z; w ^= other.w; return *this; }
		Vec4u32& operator^=(u32 other) { x ^= other; y ^= other; z ^= other; w ^= other; return *this; }

		Vec4u32& operator<<=(const Vec4u32& other) { x <<= other.x; y <<= other.y; z <<= other.z; w <<= other.w; return *this; }
		Vec4u32& operator<<=(u32 other) { x <<= other; y <<= other; z <<= other; w <<= other; return *this; }

		Vec4u32& operator>>=(const Vec4u32& other) { x >>= other.x; y >>= other.y; z >>= other.z; w >>= other.w; return *this; }
		Vec4u32& operator>>=(u32 other) { x >>= other; y >>= other; z >>= other; w >>= other; return *this; }

		Vec4u32 operator~() const { return Vec4u32(~x, ~y, ~z, ~w); }

		u32& operator[](u32 index) { return (&x)[index]; }
		const u32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = u32;
	};

	inline Vec4u32 Min(const Vec4u32& lh, const Vec4u32& rh) { return Vec4u32(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4u32 Max(const Vec4u32& lh, const Vec4u32& rh) { return Vec4u32(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Vec4u16 {
		union {
			struct { u16 x; u16 y; u16 z; u16 w; };
			struct { Vec2u16 xy; Vec2u16 zw; };
			Vec3u16 xyz;
		};

		constexpr Vec4u16() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4u16(u16 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4u16(u16 x, u16 y, u16 z, u16 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4u16(const Vec2u16& xy, u16 z, u16 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4u16(const Vec2u16& xy, const Vec2u16& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4u16(const Vec3u16& xyz, u16 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4u16(const Vec4u16& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4u16(const T& xyzw) : x((u16)xyzw.x), y((u16)xyzw.y), z((u16)xyzw.z), w((u16)xyzw.w) {}

		Vec4u16 operator+(const Vec4u16& other) const { return Vec4u16(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4u16 operator+(u16 other) const { return Vec4u16(x + other, y + other, z + other, w + other); }

		Vec4u16 operator-(const Vec4u16& other) const { return Vec4u16(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4u16 operator-(u16 other) const { return Vec4u16(x - other, y - other, z - other, w - other); }

		Vec4u16 operator*(const Vec4u16& other) const { return Vec4u16(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4u16 operator*(u16 other) const { return Vec4u16(x * other, y * other, z * other, w * other); }

		Vec4u16 operator/(const Vec4u16& other) const { return Vec4u16(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4u16 operator/(u16 other) const { return Vec4u16(x / other, y / other, z / other, w / other); }

		Vec4u16 operator%(const Vec4u16& other) const { return Vec4u16(x % other.x, y % other.y, z % other.z, w % other.w); }
		Vec4u16 operator%(u16 other) const { return Vec4u16(x % other, y % other, z % other, w % other); }

		Vec4u16 operator&(const Vec4u16& other) const { return Vec4u16(x & other.x, y & other.y, z & other.z, w & other.w); }
		Vec4u16 operator&(u16 other) const { return Vec4u16(x & other, y & other, z & other, w & other); }

		Vec4u16 operator|(const Vec4u16& other) const { return Vec4u16(x | other.x, y | other.y, z | other.z, w | other.w); }
		Vec4u16 operator|(u16 other) const { return Vec4u16(x | other, y | other, z | other, w | other); }

		Vec4u16 operator^(const Vec4u16& other) const { return Vec4u16(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w); }
		Vec4u16 operator^(u16 other) const { return Vec4u16(x ^ other, y ^ other, z ^ other, w ^ other); }

		Vec4u16 operator<<(const Vec4u16& other) const { return Vec4u16(x << other.x, y << other.y, z << other.z, w << other.w); }
		Vec4u16 operator<<(u16 other) const { return Vec4u16(x << other, y << other, z << other, w << other); }

		Vec4u16 operator>>(const Vec4u16& other) const { return Vec4u16(x >> other.x, y >> other.y, z >> other.z, w >> other.w); }
		Vec4u16 operator>>(u16 other) const { return Vec4u16(x >> other, y >> other, z >> other, w >> other); }

		Vec4u16& operator+=(const Vec4u16& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4u16& operator+=(u16 other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4u16& operator-=(const Vec4u16& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4u16& operator-=(u16 other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4u16& operator*=(const Vec4u16& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4u16& operator*=(u16 other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4u16& operator/=(const Vec4u16& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4u16& operator/=(u16 other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4u16& operator%=(const Vec4u16& other) { x %= other.x; y %= other.y; z %= other.z; w %= other.w; return *this; }
		Vec4u16& operator%=(u16 other) { x %= other; y %= other; z %= other; w %= other; return *this; }

		Vec4u16& operator&=(const Vec4u16& other) { x &= other.x; y &= other.y; z &= other.z; w &= other.w; return *this; }
		Vec4u16& operator&=(u16 other) { x &= other; y &= other; z &= other; w &= other; return *this; }

		Vec4u16& operator|=(const Vec4u16& other) { x |= other.x; y |= other.y; z |= other.z; w |= other.w; return *this; }
		Vec4u16& operator|=(u16 other) { x |= other; y |= other; z |= other; w |= other; return *this; }

		Vec4u16& operator^=(const Vec4u16& other) { x ^= other.x; y ^= other.y; z ^= other.z; w ^= other.w; return *this; }
		Vec4u16& operator^=(u16 other) { x ^= other; y ^= other; z ^= other; w ^= other; return *this; }

		Vec4u16& operator<<=(const Vec4u16& other) { x <<= other.x; y <<= other.y; z <<= other.z; w <<= other.w; return *this; }
		Vec4u16& operator<<=(u16 other) { x <<= other; y <<= other; z <<= other; w <<= other; return *this; }

		Vec4u16& operator>>=(const Vec4u16& other) { x >>= other.x; y >>= other.y; z >>= other.z; w >>= other.w; return *this; }
		Vec4u16& operator>>=(u16 other) { x >>= other; y >>= other; z >>= other; w >>= other; return *this; }

		Vec4u16 operator~() const { return Vec4u16(~x, ~y, ~z, ~w); }

		u16& operator[](u32 index) { return (&x)[index]; }
		const u16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = u16;
	};

	inline Vec4u16 Min(const Vec4u16& lh, const Vec4u16& rh) { return Vec4u16(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4u16 Max(const Vec4u16& lh, const Vec4u16& rh) { return Vec4u16(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Vec4u8 {
		union {
			struct { u8 x; u8 y; u8 z; u8 w; };
			struct { Vec2u8 xy; Vec2u8 zw; };
			Vec3u8 xyz;
		};

		constexpr Vec4u8() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4u8(u8 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4u8(u8 x, u8 y, u8 z, u8 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4u8(const Vec2u8& xy, u8 z, u8 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4u8(const Vec2u8& xy, const Vec2u8& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4u8(const Vec3u8& xyz, u8 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4u8(const Vec4u8& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4u8(const T& xyzw) : x((u8)xyzw.x), y((u8)xyzw.y), z((u8)xyzw.z), w((u8)xyzw.w) {}

		Vec4u8 operator+(const Vec4u8& other) const { return Vec4u8(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4u8 operator+(u8 other) const { return Vec4u8(x + other, y + other, z + other, w + other); }

		Vec4u8 operator-(const Vec4u8& other) const { return Vec4u8(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4u8 operator-(u8 other) const { return Vec4u8(x - other, y - other, z - other, w - other); }

		Vec4u8 operator*(const Vec4u8& other) const { return Vec4u8(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4u8 operator*(u8 other) const { return Vec4u8(x * other, y * other, z * other, w * other); }

		Vec4u8 operator/(const Vec4u8& other) const { return Vec4u8(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4u8 operator/(u8 other) const { return Vec4u8(x / other, y / other, z / other, w / other); }

		Vec4u8 operator%(const Vec4u8& other) const { return Vec4u8(x % other.x, y % other.y, z % other.z, w % other.w); }
		Vec4u8 operator%(u8 other) const { return Vec4u8(x % other, y % other, z % other, w % other); }

		Vec4u8 operator&(const Vec4u8& other) const { return Vec4u8(x & other.x, y & other.y, z & other.z, w & other.w); }
		Vec4u8 operator&(u8 other) const { return Vec4u8(x & other, y & other, z & other, w & other); }

		Vec4u8 operator|(const Vec4u8& other) const { return Vec4u8(x | other.x, y | other.y, z | other.z, w | other.w); }
		Vec4u8 operator|(u8 other) const { return Vec4u8(x | other, y | other, z | other, w | other); }

		Vec4u8 operator^(const Vec4u8& other) const { return Vec4u8(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w); }
		Vec4u8 operator^(u8 other) const { return Vec4u8(x ^ other, y ^ other, z ^ other, w ^ other); }

		Vec4u8 operator<<(const Vec4u8& other) const { return Vec4u8(x << other.x, y << other.y, z << other.z, w << other.w); }
		Vec4u8 operator<<(u8 other) const { return Vec4u8(x << other, y << other, z << other, w << other); }

		Vec4u8 operator>>(const Vec4u8& other) const { return Vec4u8(x >> other.x, y >> other.y, z >> other.z, w >> other.w); }
		Vec4u8 operator>>(u8 other) const { return Vec4u8(x >> other, y >> other, z >> other, w >> other); }

		Vec4u8& operator+=(const Vec4u8& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4u8& operator+=(u8 other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4u8& operator-=(const Vec4u8& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4u8& operator-=(u8 other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4u8& operator*=(const Vec4u8& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4u8& operator*=(u8 other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4u8& operator/=(const Vec4u8& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4u8& operator/=(u8 other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4u8& operator%=(const Vec4u8& other) { x %= other.x; y %= other.y; z %= other.z; w %= other.w; return *this; }
		Vec4u8& operator%=(u8 other) { x %= other; y %= other; z %= other; w %= other; return *this; }

		Vec4u8& operator&=(const Vec4u8& other) { x &= other.x; y &= other.y; z &= other.z; w &= other.w; return *this; }
		Vec4u8& operator&=(u8 other) { x &= other; y &= other; z &= other; w &= other; return *this; }

		Vec4u8& operator|=(const Vec4u8& other) { x |= other.x; y |= other.y; z |= other.z; w |= other.w; return *this; }
		Vec4u8& operator|=(u8 other) { x |= other; y |= other; z |= other; w |= other; return *this; }

		Vec4u8& operator^=(const Vec4u8& other) { x ^= other.x; y ^= other.y; z ^= other.z; w ^= other.w; return *this; }
		Vec4u8& operator^=(u8 other) { x ^= other; y ^= other; z ^= other; w ^= other; return *this; }

		Vec4u8& operator<<=(const Vec4u8& other) { x <<= other.x; y <<= other.y; z <<= other.z; w <<= other.w; return *this; }
		Vec4u8& operator<<=(u8 other) { x <<= other; y <<= other; z <<= other; w <<= other; return *this; }

		Vec4u8& operator>>=(const Vec4u8& other) { x >>= other.x; y >>= other.y; z >>= other.z; w >>= other.w; return *this; }
		Vec4u8& operator>>=(u8 other) { x >>= other; y >>= other; z >>= other; w >>= other; return *this; }

		Vec4u8 operator~() const { return Vec4u8(~x, ~y, ~z, ~w); }

		u8& operator[](u32 index) { return (&x)[index]; }
		const u8& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = u8;
	};

	inline Vec4u8 Min(const Vec4u8& lh, const Vec4u8& rh) { return Vec4u8(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4u8 Max(const Vec4u8& lh, const Vec4u8& rh) { return Vec4u8(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Vec2s32 {
		s32 x; s32 y;

		constexpr Vec2s32() : x(0), y(0) {}
		constexpr Vec2s32(s32 x) : x(x), y(x) {}
		constexpr Vec2s32(s32 x, s32 y) : x(x), y(y) {}
		constexpr Vec2s32(const Vec2s32& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2s32(const T& xy) : x((s32)xy.x), y((s32)xy.y) {}

		Vec2s32 operator+(const Vec2s32& other) const { return Vec2s32(x + other.x, y + other.y); }
		Vec2s32 operator+(s32 other) const { return Vec2s32(x + other, y + other); }

		Vec2s32 operator-(const Vec2s32& other) const { return Vec2s32(x - other.x, y - other.y); }
		Vec2s32 operator-(s32 other) const { return Vec2s32(x - other, y - other); }

		Vec2s32 operator*(const Vec2s32& other) const { return Vec2s32(x * other.x, y * other.y); }
		Vec2s32 operator*(s32 other) const { return Vec2s32(x * other, y * other); }

		Vec2s32 operator/(const Vec2s32& other) const { return Vec2s32(x / other.x, y / other.y); }
		Vec2s32 operator/(s32 other) const { return Vec2s32(x / other, y / other); }

		Vec2s32 operator%(const Vec2s32& other) const { return Vec2s32(x % other.x, y % other.y); }
		Vec2s32 operator%(s32 other) const { return Vec2s32(x % other, y % other); }

		Vec2s32 operator&(const Vec2s32& other) const { return Vec2s32(x & other.x, y & other.y); }
		Vec2s32 operator&(s32 other) const { return Vec2s32(x & other, y & other); }

		Vec2s32 operator|(const Vec2s32& other) const { return Vec2s32(x | other.x, y | other.y); }
		Vec2s32 operator|(s32 other) const { return Vec2s32(x | other, y | other); }

		Vec2s32 operator^(const Vec2s32& other) const { return Vec2s32(x ^ other.x, y ^ other.y); }
		Vec2s32 operator^(s32 other) const { return Vec2s32(x ^ other, y ^ other); }

		Vec2s32 operator<<(const Vec2s32& other) const { return Vec2s32(x << other.x, y << other.y); }
		Vec2s32 operator<<(s32 other) const { return Vec2s32(x << other, y << other); }

		Vec2s32 operator>>(const Vec2s32& other) const { return Vec2s32(x >> other.x, y >> other.y); }
		Vec2s32 operator>>(s32 other) const { return Vec2s32(x >> other, y >> other); }

		Vec2s32& operator+=(const Vec2s32& other) { x += other.x; y += other.y; return *this; }
		Vec2s32& operator+=(s32 other) { x += other; y += other; return *this; }

		Vec2s32& operator-=(const Vec2s32& other) { x -= other.x; y -= other.y; return *this; }
		Vec2s32& operator-=(s32 other) { x -= other; y -= other; return *this; }

		Vec2s32& operator*=(const Vec2s32& other) { x *= other.x; y *= other.y; return *this; }
		Vec2s32& operator*=(s32 other) { x *= other; y *= other; return *this; }

		Vec2s32& operator/=(const Vec2s32& other) { x /= other.x; y /= other.y; return *this; }
		Vec2s32& operator/=(s32 other) { x /= other; y /= other; return *this; }

		Vec2s32& operator%=(const Vec2s32& other) { x %= other.x; y %= other.y; return *this; }
		Vec2s32& operator%=(s32 other) { x %= other; y %= other; return *this; }

		Vec2s32& operator&=(const Vec2s32& other) { x &= other.x; y &= other.y; return *this; }
		Vec2s32& operator&=(s32 other) { x &= other; y &= other; return *this; }

		Vec2s32& operator|=(const Vec2s32& other) { x |= other.x; y |= other.y; return *this; }
		Vec2s32& operator|=(s32 other) { x |= other; y |= other; return *this; }

		Vec2s32& operator^=(const Vec2s32& other) { x ^= other.x; y ^= other.y; return *this; }
		Vec2s32& operator^=(s32 other) { x ^= other; y ^= other; return *this; }

		Vec2s32& operator<<=(const Vec2s32& other) { x <<= other.x; y <<= other.y; return *this; }
		Vec2s32& operator<<=(s32 other) { x <<= other; y <<= other; return *this; }

		Vec2s32& operator>>=(const Vec2s32& other) { x >>= other.x; y >>= other.y; return *this; }
		Vec2s32& operator>>=(s32 other) { x >>= other; y >>= other; return *this; }

		Vec2s32 operator~() const { return Vec2s32(~x, ~y); }

		s32& operator[](u32 index) { return (&x)[index]; }
		const s32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = s32;
	};

	inline Vec2s32 Min(const Vec2s32& lh, const Vec2s32& rh) { return Vec2s32(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2s32 Max(const Vec2s32& lh, const Vec2s32& rh) { return Vec2s32(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec2s16 {
		s16 x; s16 y;

		constexpr Vec2s16() : x(0), y(0) {}
		constexpr Vec2s16(s16 x) : x(x), y(x) {}
		constexpr Vec2s16(s16 x, s16 y) : x(x), y(y) {}
		constexpr Vec2s16(const Vec2s16& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2s16(const T& xy) : x((s16)xy.x), y((s16)xy.y) {}

		Vec2s16 operator+(const Vec2s16& other) const { return Vec2s16(x + other.x, y + other.y); }
		Vec2s16 operator+(s16 other) const { return Vec2s16(x + other, y + other); }

		Vec2s16 operator-(const Vec2s16& other) const { return Vec2s16(x - other.x, y - other.y); }
		Vec2s16 operator-(s16 other) const { return Vec2s16(x - other, y - other); }

		Vec2s16 operator*(const Vec2s16& other) const { return Vec2s16(x * other.x, y * other.y); }
		Vec2s16 operator*(s16 other) const { return Vec2s16(x * other, y * other); }

		Vec2s16 operator/(const Vec2s16& other) const { return Vec2s16(x / other.x, y / other.y); }
		Vec2s16 operator/(s16 other) const { return Vec2s16(x / other, y / other); }

		Vec2s16 operator%(const Vec2s16& other) const { return Vec2s16(x % other.x, y % other.y); }
		Vec2s16 operator%(s16 other) const { return Vec2s16(x % other, y % other); }

		Vec2s16 operator&(const Vec2s16& other) const { return Vec2s16(x & other.x, y & other.y); }
		Vec2s16 operator&(s16 other) const { return Vec2s16(x & other, y & other); }

		Vec2s16 operator|(const Vec2s16& other) const { return Vec2s16(x | other.x, y | other.y); }
		Vec2s16 operator|(s16 other) const { return Vec2s16(x | other, y | other); }

		Vec2s16 operator^(const Vec2s16& other) const { return Vec2s16(x ^ other.x, y ^ other.y); }
		Vec2s16 operator^(s16 other) const { return Vec2s16(x ^ other, y ^ other); }

		Vec2s16 operator<<(const Vec2s16& other) const { return Vec2s16(x << other.x, y << other.y); }
		Vec2s16 operator<<(s16 other) const { return Vec2s16(x << other, y << other); }

		Vec2s16 operator>>(const Vec2s16& other) const { return Vec2s16(x >> other.x, y >> other.y); }
		Vec2s16 operator>>(s16 other) const { return Vec2s16(x >> other, y >> other); }

		Vec2s16& operator+=(const Vec2s16& other) { x += other.x; y += other.y; return *this; }
		Vec2s16& operator+=(s16 other) { x += other; y += other; return *this; }

		Vec2s16& operator-=(const Vec2s16& other) { x -= other.x; y -= other.y; return *this; }
		Vec2s16& operator-=(s16 other) { x -= other; y -= other; return *this; }

		Vec2s16& operator*=(const Vec2s16& other) { x *= other.x; y *= other.y; return *this; }
		Vec2s16& operator*=(s16 other) { x *= other; y *= other; return *this; }

		Vec2s16& operator/=(const Vec2s16& other) { x /= other.x; y /= other.y; return *this; }
		Vec2s16& operator/=(s16 other) { x /= other; y /= other; return *this; }

		Vec2s16& operator%=(const Vec2s16& other) { x %= other.x; y %= other.y; return *this; }
		Vec2s16& operator%=(s16 other) { x %= other; y %= other; return *this; }

		Vec2s16& operator&=(const Vec2s16& other) { x &= other.x; y &= other.y; return *this; }
		Vec2s16& operator&=(s16 other) { x &= other; y &= other; return *this; }

		Vec2s16& operator|=(const Vec2s16& other) { x |= other.x; y |= other.y; return *this; }
		Vec2s16& operator|=(s16 other) { x |= other; y |= other; return *this; }

		Vec2s16& operator^=(const Vec2s16& other) { x ^= other.x; y ^= other.y; return *this; }
		Vec2s16& operator^=(s16 other) { x ^= other; y ^= other; return *this; }

		Vec2s16& operator<<=(const Vec2s16& other) { x <<= other.x; y <<= other.y; return *this; }
		Vec2s16& operator<<=(s16 other) { x <<= other; y <<= other; return *this; }

		Vec2s16& operator>>=(const Vec2s16& other) { x >>= other.x; y >>= other.y; return *this; }
		Vec2s16& operator>>=(s16 other) { x >>= other; y >>= other; return *this; }

		Vec2s16 operator~() const { return Vec2s16(~x, ~y); }

		s16& operator[](u32 index) { return (&x)[index]; }
		const s16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = s16;
	};

	inline Vec2s16 Min(const Vec2s16& lh, const Vec2s16& rh) { return Vec2s16(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2s16 Max(const Vec2s16& lh, const Vec2s16& rh) { return Vec2s16(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec2s8 {
		s8 x; s8 y;

		constexpr Vec2s8() : x(0), y(0) {}
		constexpr Vec2s8(s8 x) : x(x), y(x) {}
		constexpr Vec2s8(s8 x, s8 y) : x(x), y(y) {}
		constexpr Vec2s8(const Vec2s8& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2s8(const T& xy) : x((s8)xy.x), y((s8)xy.y) {}

		Vec2s8 operator+(const Vec2s8& other) const { return Vec2s8(x + other.x, y + other.y); }
		Vec2s8 operator+(s8 other) const { return Vec2s8(x + other, y + other); }

		Vec2s8 operator-(const Vec2s8& other) const { return Vec2s8(x - other.x, y - other.y); }
		Vec2s8 operator-(s8 other) const { return Vec2s8(x - other, y - other); }

		Vec2s8 operator*(const Vec2s8& other) const { return Vec2s8(x * other.x, y * other.y); }
		Vec2s8 operator*(s8 other) const { return Vec2s8(x * other, y * other); }

		Vec2s8 operator/(const Vec2s8& other) const { return Vec2s8(x / other.x, y / other.y); }
		Vec2s8 operator/(s8 other) const { return Vec2s8(x / other, y / other); }

		Vec2s8 operator%(const Vec2s8& other) const { return Vec2s8(x % other.x, y % other.y); }
		Vec2s8 operator%(s8 other) const { return Vec2s8(x % other, y % other); }

		Vec2s8 operator&(const Vec2s8& other) const { return Vec2s8(x & other.x, y & other.y); }
		Vec2s8 operator&(s8 other) const { return Vec2s8(x & other, y & other); }

		Vec2s8 operator|(const Vec2s8& other) const { return Vec2s8(x | other.x, y | other.y); }
		Vec2s8 operator|(s8 other) const { return Vec2s8(x | other, y | other); }

		Vec2s8 operator^(const Vec2s8& other) const { return Vec2s8(x ^ other.x, y ^ other.y); }
		Vec2s8 operator^(s8 other) const { return Vec2s8(x ^ other, y ^ other); }

		Vec2s8 operator<<(const Vec2s8& other) const { return Vec2s8(x << other.x, y << other.y); }
		Vec2s8 operator<<(s8 other) const { return Vec2s8(x << other, y << other); }

		Vec2s8 operator>>(const Vec2s8& other) const { return Vec2s8(x >> other.x, y >> other.y); }
		Vec2s8 operator>>(s8 other) const { return Vec2s8(x >> other, y >> other); }

		Vec2s8& operator+=(const Vec2s8& other) { x += other.x; y += other.y; return *this; }
		Vec2s8& operator+=(s8 other) { x += other; y += other; return *this; }

		Vec2s8& operator-=(const Vec2s8& other) { x -= other.x; y -= other.y; return *this; }
		Vec2s8& operator-=(s8 other) { x -= other; y -= other; return *this; }

		Vec2s8& operator*=(const Vec2s8& other) { x *= other.x; y *= other.y; return *this; }
		Vec2s8& operator*=(s8 other) { x *= other; y *= other; return *this; }

		Vec2s8& operator/=(const Vec2s8& other) { x /= other.x; y /= other.y; return *this; }
		Vec2s8& operator/=(s8 other) { x /= other; y /= other; return *this; }

		Vec2s8& operator%=(const Vec2s8& other) { x %= other.x; y %= other.y; return *this; }
		Vec2s8& operator%=(s8 other) { x %= other; y %= other; return *this; }

		Vec2s8& operator&=(const Vec2s8& other) { x &= other.x; y &= other.y; return *this; }
		Vec2s8& operator&=(s8 other) { x &= other; y &= other; return *this; }

		Vec2s8& operator|=(const Vec2s8& other) { x |= other.x; y |= other.y; return *this; }
		Vec2s8& operator|=(s8 other) { x |= other; y |= other; return *this; }

		Vec2s8& operator^=(const Vec2s8& other) { x ^= other.x; y ^= other.y; return *this; }
		Vec2s8& operator^=(s8 other) { x ^= other; y ^= other; return *this; }

		Vec2s8& operator<<=(const Vec2s8& other) { x <<= other.x; y <<= other.y; return *this; }
		Vec2s8& operator<<=(s8 other) { x <<= other; y <<= other; return *this; }

		Vec2s8& operator>>=(const Vec2s8& other) { x >>= other.x; y >>= other.y; return *this; }
		Vec2s8& operator>>=(s8 other) { x >>= other; y >>= other; return *this; }

		Vec2s8 operator~() const { return Vec2s8(~x, ~y); }

		s8& operator[](u32 index) { return (&x)[index]; }
		const s8& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = s8;
	};

	inline Vec2s8 Min(const Vec2s8& lh, const Vec2s8& rh) { return Vec2s8(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2s8 Max(const Vec2s8& lh, const Vec2s8& rh) { return Vec2s8(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec3s32 {
		union {
			struct { s32 x; s32 y; s32 z; };
			Vec2s32 xy;
		};

		constexpr Vec3s32() : x(0), y(0), z(0) {}
		constexpr Vec3s32(s32 x) : x(x), y(x), z(x) {}
		constexpr Vec3s32(s32 x, s32 y, s32 z) : x(x), y(y), z(z) {}
		constexpr Vec3s32(const Vec2s32& xy, s32 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3s32(const Vec3s32& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3s32(const T& xyz) : x((s32)xyz.x), y((s32)xyz.y), z((s32)xyz.z) {}

		Vec3s32 operator+(const Vec3s32& other) const { return Vec3s32(x + other.x, y + other.y, z + other.z); }
		Vec3s32 operator+(s32 other) const { return Vec3s32(x + other, y + other, z + other); }

		Vec3s32 operator-(const Vec3s32& other) const { return Vec3s32(x - other.x, y - other.y, z - other.z); }
		Vec3s32 operator-(s32 other) const { return Vec3s32(x - other, y - other, z - other); }

		Vec3s32 operator*(const Vec3s32& other) const { return Vec3s32(x * other.x, y * other.y, z * other.z); }
		Vec3s32 operator*(s32 other) const { return Vec3s32(x * other, y * other, z * other); }

		Vec3s32 operator/(const Vec3s32& other) const { return Vec3s32(x / other.x, y / other.y, z / other.z); }
		Vec3s32 operator/(s32 other) const { return Vec3s32(x / other, y / other, z / other); }

		Vec3s32 operator%(const Vec3s32& other) const { return Vec3s32(x % other.x, y % other.y, z % other.z); }
		Vec3s32 operator%(s32 other) const { return Vec3s32(x % other, y % other, z % other); }

		Vec3s32 operator&(const Vec3s32& other) const { return Vec3s32(x & other.x, y & other.y, z & other.z); }
		Vec3s32 operator&(s32 other) const { return Vec3s32(x & other, y & other, z & other); }

		Vec3s32 operator|(const Vec3s32& other) const { return Vec3s32(x | other.x, y | other.y, z | other.z); }
		Vec3s32 operator|(s32 other) const { return Vec3s32(x | other, y | other, z | other); }

		Vec3s32 operator^(const Vec3s32& other) const { return Vec3s32(x ^ other.x, y ^ other.y, z ^ other.z); }
		Vec3s32 operator^(s32 other) const { return Vec3s32(x ^ other, y ^ other, z ^ other); }

		Vec3s32 operator<<(const Vec3s32& other) const { return Vec3s32(x << other.x, y << other.y, z << other.z); }
		Vec3s32 operator<<(s32 other) const { return Vec3s32(x << other, y << other, z << other); }

		Vec3s32 operator>>(const Vec3s32& other) const { return Vec3s32(x >> other.x, y >> other.y, z >> other.z); }
		Vec3s32 operator>>(s32 other) const { return Vec3s32(x >> other, y >> other, z >> other); }

		Vec3s32& operator+=(const Vec3s32& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3s32& operator+=(s32 other) { x += other; y += other; z += other; return *this; }

		Vec3s32& operator-=(const Vec3s32& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3s32& operator-=(s32 other) { x -= other; y -= other; z -= other; return *this; }

		Vec3s32& operator*=(const Vec3s32& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3s32& operator*=(s32 other) { x *= other; y *= other; z *= other; return *this; }

		Vec3s32& operator/=(const Vec3s32& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3s32& operator/=(s32 other) { x /= other; y /= other; z /= other; return *this; }

		Vec3s32& operator%=(const Vec3s32& other) { x %= other.x; y %= other.y; z %= other.z; return *this; }
		Vec3s32& operator%=(s32 other) { x %= other; y %= other; z %= other; return *this; }

		Vec3s32& operator&=(const Vec3s32& other) { x &= other.x; y &= other.y; z &= other.z; return *this; }
		Vec3s32& operator&=(s32 other) { x &= other; y &= other; z &= other; return *this; }

		Vec3s32& operator|=(const Vec3s32& other) { x |= other.x; y |= other.y; z |= other.z; return *this; }
		Vec3s32& operator|=(s32 other) { x |= other; y |= other; z |= other; return *this; }

		Vec3s32& operator^=(const Vec3s32& other) { x ^= other.x; y ^= other.y; z ^= other.z; return *this; }
		Vec3s32& operator^=(s32 other) { x ^= other; y ^= other; z ^= other; return *this; }

		Vec3s32& operator<<=(const Vec3s32& other) { x <<= other.x; y <<= other.y; z <<= other.z; return *this; }
		Vec3s32& operator<<=(s32 other) { x <<= other; y <<= other; z <<= other; return *this; }

		Vec3s32& operator>>=(const Vec3s32& other) { x >>= other.x; y >>= other.y; z >>= other.z; return *this; }
		Vec3s32& operator>>=(s32 other) { x >>= other; y >>= other; z >>= other; return *this; }

		Vec3s32 operator~() const { return Vec3s32(~x, ~y, ~z); }

		s32& operator[](u32 index) { return (&x)[index]; }
		const s32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = s32;
	};

	inline Vec3s32 Min(const Vec3s32& lh, const Vec3s32& rh) { return Vec3s32(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3s32 Max(const Vec3s32& lh, const Vec3s32& rh) { return Vec3s32(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec3s16 {
		union {
			struct { s16 x; s16 y; s16 z; };
			Vec2s16 xy;
		};

		constexpr Vec3s16() : x(0), y(0), z(0) {}
		constexpr Vec3s16(s16 x) : x(x), y(x), z(x) {}
		constexpr Vec3s16(s16 x, s16 y, s16 z) : x(x), y(y), z(z) {}
		constexpr Vec3s16(const Vec2s16& xy, s16 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3s16(const Vec3s16& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3s16(const T& xyz) : x((s16)xyz.x), y((s16)xyz.y), z((s16)xyz.z) {}

		Vec3s16 operator+(const Vec3s16& other) const { return Vec3s16(x + other.x, y + other.y, z + other.z); }
		Vec3s16 operator+(s16 other) const { return Vec3s16(x + other, y + other, z + other); }

		Vec3s16 operator-(const Vec3s16& other) const { return Vec3s16(x - other.x, y - other.y, z - other.z); }
		Vec3s16 operator-(s16 other) const { return Vec3s16(x - other, y - other, z - other); }

		Vec3s16 operator*(const Vec3s16& other) const { return Vec3s16(x * other.x, y * other.y, z * other.z); }
		Vec3s16 operator*(s16 other) const { return Vec3s16(x * other, y * other, z * other); }

		Vec3s16 operator/(const Vec3s16& other) const { return Vec3s16(x / other.x, y / other.y, z / other.z); }
		Vec3s16 operator/(s16 other) const { return Vec3s16(x / other, y / other, z / other); }

		Vec3s16 operator%(const Vec3s16& other) const { return Vec3s16(x % other.x, y % other.y, z % other.z); }
		Vec3s16 operator%(s16 other) const { return Vec3s16(x % other, y % other, z % other); }

		Vec3s16 operator&(const Vec3s16& other) const { return Vec3s16(x & other.x, y & other.y, z & other.z); }
		Vec3s16 operator&(s16 other) const { return Vec3s16(x & other, y & other, z & other); }

		Vec3s16 operator|(const Vec3s16& other) const { return Vec3s16(x | other.x, y | other.y, z | other.z); }
		Vec3s16 operator|(s16 other) const { return Vec3s16(x | other, y | other, z | other); }

		Vec3s16 operator^(const Vec3s16& other) const { return Vec3s16(x ^ other.x, y ^ other.y, z ^ other.z); }
		Vec3s16 operator^(s16 other) const { return Vec3s16(x ^ other, y ^ other, z ^ other); }

		Vec3s16 operator<<(const Vec3s16& other) const { return Vec3s16(x << other.x, y << other.y, z << other.z); }
		Vec3s16 operator<<(s16 other) const { return Vec3s16(x << other, y << other, z << other); }

		Vec3s16 operator>>(const Vec3s16& other) const { return Vec3s16(x >> other.x, y >> other.y, z >> other.z); }
		Vec3s16 operator>>(s16 other) const { return Vec3s16(x >> other, y >> other, z >> other); }

		Vec3s16& operator+=(const Vec3s16& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3s16& operator+=(s16 other) { x += other; y += other; z += other; return *this; }

		Vec3s16& operator-=(const Vec3s16& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3s16& operator-=(s16 other) { x -= other; y -= other; z -= other; return *this; }

		Vec3s16& operator*=(const Vec3s16& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3s16& operator*=(s16 other) { x *= other; y *= other; z *= other; return *this; }

		Vec3s16& operator/=(const Vec3s16& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3s16& operator/=(s16 other) { x /= other; y /= other; z /= other; return *this; }

		Vec3s16& operator%=(const Vec3s16& other) { x %= other.x; y %= other.y; z %= other.z; return *this; }
		Vec3s16& operator%=(s16 other) { x %= other; y %= other; z %= other; return *this; }

		Vec3s16& operator&=(const Vec3s16& other) { x &= other.x; y &= other.y; z &= other.z; return *this; }
		Vec3s16& operator&=(s16 other) { x &= other; y &= other; z &= other; return *this; }

		Vec3s16& operator|=(const Vec3s16& other) { x |= other.x; y |= other.y; z |= other.z; return *this; }
		Vec3s16& operator|=(s16 other) { x |= other; y |= other; z |= other; return *this; }

		Vec3s16& operator^=(const Vec3s16& other) { x ^= other.x; y ^= other.y; z ^= other.z; return *this; }
		Vec3s16& operator^=(s16 other) { x ^= other; y ^= other; z ^= other; return *this; }

		Vec3s16& operator<<=(const Vec3s16& other) { x <<= other.x; y <<= other.y; z <<= other.z; return *this; }
		Vec3s16& operator<<=(s16 other) { x <<= other; y <<= other; z <<= other; return *this; }

		Vec3s16& operator>>=(const Vec3s16& other) { x >>= other.x; y >>= other.y; z >>= other.z; return *this; }
		Vec3s16& operator>>=(s16 other) { x >>= other; y >>= other; z >>= other; return *this; }

		Vec3s16 operator~() const { return Vec3s16(~x, ~y, ~z); }

		s16& operator[](u32 index) { return (&x)[index]; }
		const s16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = s16;
	};

	inline Vec3s16 Min(const Vec3s16& lh, const Vec3s16& rh) { return Vec3s16(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3s16 Max(const Vec3s16& lh, const Vec3s16& rh) { return Vec3s16(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec3s8 {
		union {
			struct { s8 x; s8 y; s8 z; };
			Vec2s8 xy;
		};

		constexpr Vec3s8() : x(0), y(0), z(0) {}
		constexpr Vec3s8(s8 x) : x(x), y(x), z(x) {}
		constexpr Vec3s8(s8 x, s8 y, s8 z) : x(x), y(y), z(z) {}
		constexpr Vec3s8(const Vec2s8& xy, s8 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3s8(const Vec3s8& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3s8(const T& xyz) : x((s8)xyz.x), y((s8)xyz.y), z((s8)xyz.z) {}

		Vec3s8 operator+(const Vec3s8& other) const { return Vec3s8(x + other.x, y + other.y, z + other.z); }
		Vec3s8 operator+(s8 other) const { return Vec3s8(x + other, y + other, z + other); }

		Vec3s8 operator-(const Vec3s8& other) const { return Vec3s8(x - other.x, y - other.y, z - other.z); }
		Vec3s8 operator-(s8 other) const { return Vec3s8(x - other, y - other, z - other); }

		Vec3s8 operator*(const Vec3s8& other) const { return Vec3s8(x * other.x, y * other.y, z * other.z); }
		Vec3s8 operator*(s8 other) const { return Vec3s8(x * other, y * other, z * other); }

		Vec3s8 operator/(const Vec3s8& other) const { return Vec3s8(x / other.x, y / other.y, z / other.z); }
		Vec3s8 operator/(s8 other) const { return Vec3s8(x / other, y / other, z / other); }

		Vec3s8 operator%(const Vec3s8& other) const { return Vec3s8(x % other.x, y % other.y, z % other.z); }
		Vec3s8 operator%(s8 other) const { return Vec3s8(x % other, y % other, z % other); }

		Vec3s8 operator&(const Vec3s8& other) const { return Vec3s8(x & other.x, y & other.y, z & other.z); }
		Vec3s8 operator&(s8 other) const { return Vec3s8(x & other, y & other, z & other); }

		Vec3s8 operator|(const Vec3s8& other) const { return Vec3s8(x | other.x, y | other.y, z | other.z); }
		Vec3s8 operator|(s8 other) const { return Vec3s8(x | other, y | other, z | other); }

		Vec3s8 operator^(const Vec3s8& other) const { return Vec3s8(x ^ other.x, y ^ other.y, z ^ other.z); }
		Vec3s8 operator^(s8 other) const { return Vec3s8(x ^ other, y ^ other, z ^ other); }

		Vec3s8 operator<<(const Vec3s8& other) const { return Vec3s8(x << other.x, y << other.y, z << other.z); }
		Vec3s8 operator<<(s8 other) const { return Vec3s8(x << other, y << other, z << other); }

		Vec3s8 operator>>(const Vec3s8& other) const { return Vec3s8(x >> other.x, y >> other.y, z >> other.z); }
		Vec3s8 operator>>(s8 other) const { return Vec3s8(x >> other, y >> other, z >> other); }

		Vec3s8& operator+=(const Vec3s8& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3s8& operator+=(s8 other) { x += other; y += other; z += other; return *this; }

		Vec3s8& operator-=(const Vec3s8& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3s8& operator-=(s8 other) { x -= other; y -= other; z -= other; return *this; }

		Vec3s8& operator*=(const Vec3s8& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3s8& operator*=(s8 other) { x *= other; y *= other; z *= other; return *this; }

		Vec3s8& operator/=(const Vec3s8& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3s8& operator/=(s8 other) { x /= other; y /= other; z /= other; return *this; }

		Vec3s8& operator%=(const Vec3s8& other) { x %= other.x; y %= other.y; z %= other.z; return *this; }
		Vec3s8& operator%=(s8 other) { x %= other; y %= other; z %= other; return *this; }

		Vec3s8& operator&=(const Vec3s8& other) { x &= other.x; y &= other.y; z &= other.z; return *this; }
		Vec3s8& operator&=(s8 other) { x &= other; y &= other; z &= other; return *this; }

		Vec3s8& operator|=(const Vec3s8& other) { x |= other.x; y |= other.y; z |= other.z; return *this; }
		Vec3s8& operator|=(s8 other) { x |= other; y |= other; z |= other; return *this; }

		Vec3s8& operator^=(const Vec3s8& other) { x ^= other.x; y ^= other.y; z ^= other.z; return *this; }
		Vec3s8& operator^=(s8 other) { x ^= other; y ^= other; z ^= other; return *this; }

		Vec3s8& operator<<=(const Vec3s8& other) { x <<= other.x; y <<= other.y; z <<= other.z; return *this; }
		Vec3s8& operator<<=(s8 other) { x <<= other; y <<= other; z <<= other; return *this; }

		Vec3s8& operator>>=(const Vec3s8& other) { x >>= other.x; y >>= other.y; z >>= other.z; return *this; }
		Vec3s8& operator>>=(s8 other) { x >>= other; y >>= other; z >>= other; return *this; }

		Vec3s8 operator~() const { return Vec3s8(~x, ~y, ~z); }

		s8& operator[](u32 index) { return (&x)[index]; }
		const s8& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = s8;
	};

	inline Vec3s8 Min(const Vec3s8& lh, const Vec3s8& rh) { return Vec3s8(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3s8 Max(const Vec3s8& lh, const Vec3s8& rh) { return Vec3s8(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec4s32 {
		union {
			struct { s32 x; s32 y; s32 z; s32 w; };
			struct { Vec2s32 xy; Vec2s32 zw; };
			Vec3s32 xyz;
		};

		constexpr Vec4s32() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4s32(s32 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4s32(s32 x, s32 y, s32 z, s32 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4s32(const Vec2s32& xy, s32 z, s32 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4s32(const Vec2s32& xy, const Vec2s32& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4s32(const Vec3s32& xyz, s32 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4s32(const Vec4s32& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4s32(const T& xyzw) : x((s32)xyzw.x), y((s32)xyzw.y), z((s32)xyzw.z), w((s32)xyzw.w) {}

		Vec4s32 operator+(const Vec4s32& other) const { return Vec4s32(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4s32 operator+(s32 other) const { return Vec4s32(x + other, y + other, z + other, w + other); }

		Vec4s32 operator-(const Vec4s32& other) const { return Vec4s32(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4s32 operator-(s32 other) const { return Vec4s32(x - other, y - other, z - other, w - other); }

		Vec4s32 operator*(const Vec4s32& other) const { return Vec4s32(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4s32 operator*(s32 other) const { return Vec4s32(x * other, y * other, z * other, w * other); }

		Vec4s32 operator/(const Vec4s32& other) const { return Vec4s32(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4s32 operator/(s32 other) const { return Vec4s32(x / other, y / other, z / other, w / other); }

		Vec4s32 operator%(const Vec4s32& other) const { return Vec4s32(x % other.x, y % other.y, z % other.z, w % other.w); }
		Vec4s32 operator%(s32 other) const { return Vec4s32(x % other, y % other, z % other, w % other); }

		Vec4s32 operator&(const Vec4s32& other) const { return Vec4s32(x & other.x, y & other.y, z & other.z, w & other.w); }
		Vec4s32 operator&(s32 other) const { return Vec4s32(x & other, y & other, z & other, w & other); }

		Vec4s32 operator|(const Vec4s32& other) const { return Vec4s32(x | other.x, y | other.y, z | other.z, w | other.w); }
		Vec4s32 operator|(s32 other) const { return Vec4s32(x | other, y | other, z | other, w | other); }

		Vec4s32 operator^(const Vec4s32& other) const { return Vec4s32(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w); }
		Vec4s32 operator^(s32 other) const { return Vec4s32(x ^ other, y ^ other, z ^ other, w ^ other); }

		Vec4s32 operator<<(const Vec4s32& other) const { return Vec4s32(x << other.x, y << other.y, z << other.z, w << other.w); }
		Vec4s32 operator<<(s32 other) const { return Vec4s32(x << other, y << other, z << other, w << other); }

		Vec4s32 operator>>(const Vec4s32& other) const { return Vec4s32(x >> other.x, y >> other.y, z >> other.z, w >> other.w); }
		Vec4s32 operator>>(s32 other) const { return Vec4s32(x >> other, y >> other, z >> other, w >> other); }

		Vec4s32& operator+=(const Vec4s32& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4s32& operator+=(s32 other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4s32& operator-=(const Vec4s32& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4s32& operator-=(s32 other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4s32& operator*=(const Vec4s32& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4s32& operator*=(s32 other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4s32& operator/=(const Vec4s32& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4s32& operator/=(s32 other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4s32& operator%=(const Vec4s32& other) { x %= other.x; y %= other.y; z %= other.z; w %= other.w; return *this; }
		Vec4s32& operator%=(s32 other) { x %= other; y %= other; z %= other; w %= other; return *this; }

		Vec4s32& operator&=(const Vec4s32& other) { x &= other.x; y &= other.y; z &= other.z; w &= other.w; return *this; }
		Vec4s32& operator&=(s32 other) { x &= other; y &= other; z &= other; w &= other; return *this; }

		Vec4s32& operator|=(const Vec4s32& other) { x |= other.x; y |= other.y; z |= other.z; w |= other.w; return *this; }
		Vec4s32& operator|=(s32 other) { x |= other; y |= other; z |= other; w |= other; return *this; }

		Vec4s32& operator^=(const Vec4s32& other) { x ^= other.x; y ^= other.y; z ^= other.z; w ^= other.w; return *this; }
		Vec4s32& operator^=(s32 other) { x ^= other; y ^= other; z ^= other; w ^= other; return *this; }

		Vec4s32& operator<<=(const Vec4s32& other) { x <<= other.x; y <<= other.y; z <<= other.z; w <<= other.w; return *this; }
		Vec4s32& operator<<=(s32 other) { x <<= other; y <<= other; z <<= other; w <<= other; return *this; }

		Vec4s32& operator>>=(const Vec4s32& other) { x >>= other.x; y >>= other.y; z >>= other.z; w >>= other.w; return *this; }
		Vec4s32& operator>>=(s32 other) { x >>= other; y >>= other; z >>= other; w >>= other; return *this; }

		Vec4s32 operator~() const { return Vec4s32(~x, ~y, ~z, ~w); }

		s32& operator[](u32 index) { return (&x)[index]; }
		const s32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = s32;
	};

	inline Vec4s32 Min(const Vec4s32& lh, const Vec4s32& rh) { return Vec4s32(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4s32 Max(const Vec4s32& lh, const Vec4s32& rh) { return Vec4s32(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Vec4s16 {
		union {
			struct { s16 x; s16 y; s16 z; s16 w; };
			struct { Vec2s16 xy; Vec2s16 zw; };
			Vec3s16 xyz;
		};

		constexpr Vec4s16() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4s16(s16 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4s16(s16 x, s16 y, s16 z, s16 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4s16(const Vec2s16& xy, s16 z, s16 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4s16(const Vec2s16& xy, const Vec2s16& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4s16(const Vec3s16& xyz, s16 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4s16(const Vec4s16& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4s16(const T& xyzw) : x((s16)xyzw.x), y((s16)xyzw.y), z((s16)xyzw.z), w((s16)xyzw.w) {}

		Vec4s16 operator+(const Vec4s16& other) const { return Vec4s16(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4s16 operator+(s16 other) const { return Vec4s16(x + other, y + other, z + other, w + other); }

		Vec4s16 operator-(const Vec4s16& other) const { return Vec4s16(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4s16 operator-(s16 other) const { return Vec4s16(x - other, y - other, z - other, w - other); }

		Vec4s16 operator*(const Vec4s16& other) const { return Vec4s16(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4s16 operator*(s16 other) const { return Vec4s16(x * other, y * other, z * other, w * other); }

		Vec4s16 operator/(const Vec4s16& other) const { return Vec4s16(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4s16 operator/(s16 other) const { return Vec4s16(x / other, y / other, z / other, w / other); }

		Vec4s16 operator%(const Vec4s16& other) const { return Vec4s16(x % other.x, y % other.y, z % other.z, w % other.w); }
		Vec4s16 operator%(s16 other) const { return Vec4s16(x % other, y % other, z % other, w % other); }

		Vec4s16 operator&(const Vec4s16& other) const { return Vec4s16(x & other.x, y & other.y, z & other.z, w & other.w); }
		Vec4s16 operator&(s16 other) const { return Vec4s16(x & other, y & other, z & other, w & other); }

		Vec4s16 operator|(const Vec4s16& other) const { return Vec4s16(x | other.x, y | other.y, z | other.z, w | other.w); }
		Vec4s16 operator|(s16 other) const { return Vec4s16(x | other, y | other, z | other, w | other); }

		Vec4s16 operator^(const Vec4s16& other) const { return Vec4s16(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w); }
		Vec4s16 operator^(s16 other) const { return Vec4s16(x ^ other, y ^ other, z ^ other, w ^ other); }

		Vec4s16 operator<<(const Vec4s16& other) const { return Vec4s16(x << other.x, y << other.y, z << other.z, w << other.w); }
		Vec4s16 operator<<(s16 other) const { return Vec4s16(x << other, y << other, z << other, w << other); }

		Vec4s16 operator>>(const Vec4s16& other) const { return Vec4s16(x >> other.x, y >> other.y, z >> other.z, w >> other.w); }
		Vec4s16 operator>>(s16 other) const { return Vec4s16(x >> other, y >> other, z >> other, w >> other); }

		Vec4s16& operator+=(const Vec4s16& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4s16& operator+=(s16 other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4s16& operator-=(const Vec4s16& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4s16& operator-=(s16 other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4s16& operator*=(const Vec4s16& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4s16& operator*=(s16 other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4s16& operator/=(const Vec4s16& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4s16& operator/=(s16 other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4s16& operator%=(const Vec4s16& other) { x %= other.x; y %= other.y; z %= other.z; w %= other.w; return *this; }
		Vec4s16& operator%=(s16 other) { x %= other; y %= other; z %= other; w %= other; return *this; }

		Vec4s16& operator&=(const Vec4s16& other) { x &= other.x; y &= other.y; z &= other.z; w &= other.w; return *this; }
		Vec4s16& operator&=(s16 other) { x &= other; y &= other; z &= other; w &= other; return *this; }

		Vec4s16& operator|=(const Vec4s16& other) { x |= other.x; y |= other.y; z |= other.z; w |= other.w; return *this; }
		Vec4s16& operator|=(s16 other) { x |= other; y |= other; z |= other; w |= other; return *this; }

		Vec4s16& operator^=(const Vec4s16& other) { x ^= other.x; y ^= other.y; z ^= other.z; w ^= other.w; return *this; }
		Vec4s16& operator^=(s16 other) { x ^= other; y ^= other; z ^= other; w ^= other; return *this; }

		Vec4s16& operator<<=(const Vec4s16& other) { x <<= other.x; y <<= other.y; z <<= other.z; w <<= other.w; return *this; }
		Vec4s16& operator<<=(s16 other) { x <<= other; y <<= other; z <<= other; w <<= other; return *this; }

		Vec4s16& operator>>=(const Vec4s16& other) { x >>= other.x; y >>= other.y; z >>= other.z; w >>= other.w; return *this; }
		Vec4s16& operator>>=(s16 other) { x >>= other; y >>= other; z >>= other; w >>= other; return *this; }

		Vec4s16 operator~() const { return Vec4s16(~x, ~y, ~z, ~w); }

		s16& operator[](u32 index) { return (&x)[index]; }
		const s16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = s16;
	};

	inline Vec4s16 Min(const Vec4s16& lh, const Vec4s16& rh) { return Vec4s16(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4s16 Max(const Vec4s16& lh, const Vec4s16& rh) { return Vec4s16(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Vec4s8 {
		union {
			struct { s8 x; s8 y; s8 z; s8 w; };
			struct { Vec2s8 xy; Vec2s8 zw; };
			Vec3s8 xyz;
		};

		constexpr Vec4s8() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4s8(s8 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4s8(s8 x, s8 y, s8 z, s8 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4s8(const Vec2s8& xy, s8 z, s8 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4s8(const Vec2s8& xy, const Vec2s8& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4s8(const Vec3s8& xyz, s8 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4s8(const Vec4s8& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4s8(const T& xyzw) : x((s8)xyzw.x), y((s8)xyzw.y), z((s8)xyzw.z), w((s8)xyzw.w) {}

		Vec4s8 operator+(const Vec4s8& other) const { return Vec4s8(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4s8 operator+(s8 other) const { return Vec4s8(x + other, y + other, z + other, w + other); }

		Vec4s8 operator-(const Vec4s8& other) const { return Vec4s8(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4s8 operator-(s8 other) const { return Vec4s8(x - other, y - other, z - other, w - other); }

		Vec4s8 operator*(const Vec4s8& other) const { return Vec4s8(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4s8 operator*(s8 other) const { return Vec4s8(x * other, y * other, z * other, w * other); }

		Vec4s8 operator/(const Vec4s8& other) const { return Vec4s8(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4s8 operator/(s8 other) const { return Vec4s8(x / other, y / other, z / other, w / other); }

		Vec4s8 operator%(const Vec4s8& other) const { return Vec4s8(x % other.x, y % other.y, z % other.z, w % other.w); }
		Vec4s8 operator%(s8 other) const { return Vec4s8(x % other, y % other, z % other, w % other); }

		Vec4s8 operator&(const Vec4s8& other) const { return Vec4s8(x & other.x, y & other.y, z & other.z, w & other.w); }
		Vec4s8 operator&(s8 other) const { return Vec4s8(x & other, y & other, z & other, w & other); }

		Vec4s8 operator|(const Vec4s8& other) const { return Vec4s8(x | other.x, y | other.y, z | other.z, w | other.w); }
		Vec4s8 operator|(s8 other) const { return Vec4s8(x | other, y | other, z | other, w | other); }

		Vec4s8 operator^(const Vec4s8& other) const { return Vec4s8(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w); }
		Vec4s8 operator^(s8 other) const { return Vec4s8(x ^ other, y ^ other, z ^ other, w ^ other); }

		Vec4s8 operator<<(const Vec4s8& other) const { return Vec4s8(x << other.x, y << other.y, z << other.z, w << other.w); }
		Vec4s8 operator<<(s8 other) const { return Vec4s8(x << other, y << other, z << other, w << other); }

		Vec4s8 operator>>(const Vec4s8& other) const { return Vec4s8(x >> other.x, y >> other.y, z >> other.z, w >> other.w); }
		Vec4s8 operator>>(s8 other) const { return Vec4s8(x >> other, y >> other, z >> other, w >> other); }

		Vec4s8& operator+=(const Vec4s8& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4s8& operator+=(s8 other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4s8& operator-=(const Vec4s8& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4s8& operator-=(s8 other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4s8& operator*=(const Vec4s8& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4s8& operator*=(s8 other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4s8& operator/=(const Vec4s8& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4s8& operator/=(s8 other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4s8& operator%=(const Vec4s8& other) { x %= other.x; y %= other.y; z %= other.z; w %= other.w; return *this; }
		Vec4s8& operator%=(s8 other) { x %= other; y %= other; z %= other; w %= other; return *this; }

		Vec4s8& operator&=(const Vec4s8& other) { x &= other.x; y &= other.y; z &= other.z; w &= other.w; return *this; }
		Vec4s8& operator&=(s8 other) { x &= other; y &= other; z &= other; w &= other; return *this; }

		Vec4s8& operator|=(const Vec4s8& other) { x |= other.x; y |= other.y; z |= other.z; w |= other.w; return *this; }
		Vec4s8& operator|=(s8 other) { x |= other; y |= other; z |= other; w |= other; return *this; }

		Vec4s8& operator^=(const Vec4s8& other) { x ^= other.x; y ^= other.y; z ^= other.z; w ^= other.w; return *this; }
		Vec4s8& operator^=(s8 other) { x ^= other; y ^= other; z ^= other; w ^= other; return *this; }

		Vec4s8& operator<<=(const Vec4s8& other) { x <<= other.x; y <<= other.y; z <<= other.z; w <<= other.w; return *this; }
		Vec4s8& operator<<=(s8 other) { x <<= other; y <<= other; z <<= other; w <<= other; return *this; }

		Vec4s8& operator>>=(const Vec4s8& other) { x >>= other.x; y >>= other.y; z >>= other.z; w >>= other.w; return *this; }
		Vec4s8& operator>>=(s8 other) { x >>= other; y >>= other; z >>= other; w >>= other; return *this; }

		Vec4s8 operator~() const { return Vec4s8(~x, ~y, ~z, ~w); }

		s8& operator[](u32 index) { return (&x)[index]; }
		const s8& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = s8;
	};

	inline Vec4s8 Min(const Vec4s8& lh, const Vec4s8& rh) { return Vec4s8(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4s8 Max(const Vec4s8& lh, const Vec4s8& rh) { return Vec4s8(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Vec2f {
		float x; float y;

		constexpr Vec2f() : x(0), y(0) {}
		constexpr Vec2f(float x) : x(x), y(x) {}
		constexpr Vec2f(float x, float y) : x(x), y(y) {}
		constexpr Vec2f(const Vec2f& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2f(const T& xy) : x((float)xy.x), y((float)xy.y) {}

		Vec2f operator+(const Vec2f& other) const { return Vec2f(x + other.x, y + other.y); }
		Vec2f operator+(float other) const { return Vec2f(x + other, y + other); }

		Vec2f operator-(const Vec2f& other) const { return Vec2f(x - other.x, y - other.y); }
		Vec2f operator-(float other) const { return Vec2f(x - other, y - other); }

		Vec2f operator*(const Vec2f& other) const { return Vec2f(x * other.x, y * other.y); }
		Vec2f operator*(float other) const { return Vec2f(x * other, y * other); }

		Vec2f operator/(const Vec2f& other) const { return Vec2f(x / other.x, y / other.y); }
		Vec2f operator/(float other) const { return Vec2f(x / other, y / other); }

		Vec2f& operator+=(const Vec2f& other) { x += other.x; y += other.y; return *this; }
		Vec2f& operator+=(float other) { x += other; y += other; return *this; }

		Vec2f& operator-=(const Vec2f& other) { x -= other.x; y -= other.y; return *this; }
		Vec2f& operator-=(float other) { x -= other; y -= other; return *this; }

		Vec2f& operator*=(const Vec2f& other) { x *= other.x; y *= other.y; return *this; }
		Vec2f& operator*=(float other) { x *= other; y *= other; return *this; }

		Vec2f& operator/=(const Vec2f& other) { x /= other.x; y /= other.y; return *this; }
		Vec2f& operator/=(float other) { x /= other; y /= other; return *this; }

		Vec2f operator-() const { return Vec2f(-x, -y); }

		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = float;
	};

	inline Vec2f Min(const Vec2f& lh, const Vec2f& rh) { return Vec2f(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2f Max(const Vec2f& lh, const Vec2f& rh) { return Vec2f(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	inline float Cross(const Vec2f& lh, const Vec2f& rh) { return lh.x * rh.y - lh.y * rh.x; }
	inline float Dot(const Vec2f& lh, const Vec2f& rh) { return lh.x * rh.x + lh.y * rh.y; }
	inline float LengthSquare(const Vec2f& v) { return Dot(v, v); }
	inline float Length(const Vec2f& v) { return sqrtf(Dot(v, v)); }
	inline Vec2f Normalize(const Vec2f& v) { return v * (1.f / Length(v)); }

	struct Vec2h {
		float16 x; float16 y;

		constexpr Vec2h() : x(0), y(0) {}
		constexpr Vec2h(float16 x) : x(x), y(x) {}
		constexpr Vec2h(float16 x, float16 y) : x(x), y(y) {}
		constexpr Vec2h(const Vec2h& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit constexpr Vec2h(const T& xy) : x((float16)xy.x), y((float16)xy.y) {}

		float16& operator[](u32 index) { return (&x)[index]; }
		const float16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 2;
		compile_const u64 capacity = 2;
		using ValueType = float16;
	};

	inline Vec2h Min(const Vec2h& lh, const Vec2h& rh) { return Vec2h(Min(lh.x, rh.x), Min(lh.y, rh.y)); }
	inline Vec2h Max(const Vec2h& lh, const Vec2h& rh) { return Vec2h(Max(lh.x, rh.x), Max(lh.y, rh.y)); }

	struct Vec3f {
		union {
			struct { float x; float y; float z; };
			Vec2f xy;
		};

		constexpr Vec3f() : x(0), y(0), z(0) {}
		constexpr Vec3f(float x) : x(x), y(x), z(x) {}
		constexpr Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}
		constexpr Vec3f(const Vec2f& xy, float z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3f(const Vec3f& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3f(const T& xyz) : x((float)xyz.x), y((float)xyz.y), z((float)xyz.z) {}

		Vec3f operator+(const Vec3f& other) const { return Vec3f(x + other.x, y + other.y, z + other.z); }
		Vec3f operator+(float other) const { return Vec3f(x + other, y + other, z + other); }

		Vec3f operator-(const Vec3f& other) const { return Vec3f(x - other.x, y - other.y, z - other.z); }
		Vec3f operator-(float other) const { return Vec3f(x - other, y - other, z - other); }

		Vec3f operator*(const Vec3f& other) const { return Vec3f(x * other.x, y * other.y, z * other.z); }
		Vec3f operator*(float other) const { return Vec3f(x * other, y * other, z * other); }

		Vec3f operator/(const Vec3f& other) const { return Vec3f(x / other.x, y / other.y, z / other.z); }
		Vec3f operator/(float other) const { return Vec3f(x / other, y / other, z / other); }

		Vec3f& operator+=(const Vec3f& other) { x += other.x; y += other.y; z += other.z; return *this; }
		Vec3f& operator+=(float other) { x += other; y += other; z += other; return *this; }

		Vec3f& operator-=(const Vec3f& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
		Vec3f& operator-=(float other) { x -= other; y -= other; z -= other; return *this; }

		Vec3f& operator*=(const Vec3f& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
		Vec3f& operator*=(float other) { x *= other; y *= other; z *= other; return *this; }

		Vec3f& operator/=(const Vec3f& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }
		Vec3f& operator/=(float other) { x /= other; y /= other; z /= other; return *this; }

		Vec3f operator-() const { return Vec3f(-x, -y, -z); }

		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = float;
	};

	inline Vec3f Min(const Vec3f& lh, const Vec3f& rh) { return Vec3f(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3f Max(const Vec3f& lh, const Vec3f& rh) { return Vec3f(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	inline Vec3f Cross(const Vec3f& lh, const Vec3f& rh) {
		Vec3f result;
		result.x = lh.y * rh.z - lh.z * rh.y;
		result.y = lh.z * rh.x - lh.x * rh.z;
		result.z = lh.x * rh.y - lh.y * rh.x;
		return result;
	}

	inline float Dot(const Vec3f& lh, const Vec3f& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z; }
	inline float LengthSquare(const Vec3f& v) { return Dot(v, v); }
	inline float Length(const Vec3f& v) { return sqrtf(Dot(v, v)); }
	inline Vec3f Normalize(const Vec3f& v) { return v * (1.f / Length(v)); }

	struct Vec3h {
		union {
			struct { float16 x; float16 y; float16 z; };
			Vec2h xy;
		};

		constexpr Vec3h() : x(0), y(0), z(0) {}
		constexpr Vec3h(float16 x) : x(x), y(x), z(x) {}
		constexpr Vec3h(float16 x, float16 y, float16 z) : x(x), y(y), z(z) {}
		constexpr Vec3h(const Vec2h& xy, float16 z) : x(xy.x), y(xy.y), z(z) {}
		constexpr Vec3h(const Vec3h& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit constexpr Vec3h(const T& xyz) : x((float16)xyz.x), y((float16)xyz.y), z((float16)xyz.z) {}

		float16& operator[](u32 index) { return (&x)[index]; }
		const float16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 3;
		compile_const u64 capacity = 3;
		using ValueType = float16;
	};

	inline Vec3h Min(const Vec3h& lh, const Vec3h& rh) { return Vec3h(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z)); }
	inline Vec3h Max(const Vec3h& lh, const Vec3h& rh) { return Vec3h(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z)); }

	struct Vec4f {
		union {
			struct { float x; float y; float z; float w; };
			struct { Vec2f xy; Vec2f zw; };
			Vec3f xyz;
		};

		constexpr Vec4f() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4f(float x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4f(const Vec2f& xy, float z, float w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4f(const Vec2f& xy, const Vec2f& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4f(const Vec3f& xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4f(const Vec4f& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4f(const T& xyzw) : x((float)xyzw.x), y((float)xyzw.y), z((float)xyzw.z), w((float)xyzw.w) {}

		Vec4f operator+(const Vec4f& other) const { return Vec4f(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4f operator+(float other) const { return Vec4f(x + other, y + other, z + other, w + other); }

		Vec4f operator-(const Vec4f& other) const { return Vec4f(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4f operator-(float other) const { return Vec4f(x - other, y - other, z - other, w - other); }

		Vec4f operator*(const Vec4f& other) const { return Vec4f(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4f operator*(float other) const { return Vec4f(x * other, y * other, z * other, w * other); }

		Vec4f operator/(const Vec4f& other) const { return Vec4f(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4f operator/(float other) const { return Vec4f(x / other, y / other, z / other, w / other); }

		Vec4f& operator+=(const Vec4f& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		Vec4f& operator+=(float other) { x += other; y += other; z += other; w += other; return *this; }

		Vec4f& operator-=(const Vec4f& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		Vec4f& operator-=(float other) { x -= other; y -= other; z -= other; w -= other; return *this; }

		Vec4f& operator*=(const Vec4f& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		Vec4f& operator*=(float other) { x *= other; y *= other; z *= other; w *= other; return *this; }

		Vec4f& operator/=(const Vec4f& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }
		Vec4f& operator/=(float other) { x /= other; y /= other; z /= other; w /= other; return *this; }

		Vec4f operator-() const { return Vec4f(-x, -y, -z, -w); }

		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = float;
	};

	inline Vec4f Min(const Vec4f& lh, const Vec4f& rh) { return Vec4f(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4f Max(const Vec4f& lh, const Vec4f& rh) { return Vec4f(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	inline float Dot(const Vec4f& lh, const Vec4f& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z + lh.w * rh.w; }
	inline float LengthSquare(const Vec4f& v) { return Dot(v, v); }
	inline float Length(const Vec4f& v) { return sqrtf(Dot(v, v)); }
	inline Vec4f Normalize(const Vec4f& v) { return v * (1.f / Length(v)); }

	struct Vec4h {
		union {
			struct { float16 x; float16 y; float16 z; float16 w; };
			struct { Vec2h xy; Vec2h zw; };
			Vec3h xyz;
		};

		constexpr Vec4h() : x(0), y(0), z(0), w(0) {}
		constexpr Vec4h(float16 x) : x(x), y(x), z(x), w(x) {}
		constexpr Vec4h(float16 x, float16 y, float16 z, float16 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4h(const Vec2h& xy, float16 z, float16 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		constexpr Vec4h(const Vec2h& xy, const Vec2h& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		constexpr Vec4h(const Vec3h& xyz, float16 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		constexpr Vec4h(const Vec4h& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit constexpr Vec4h(const T& xyzw) : x((float16)xyzw.x), y((float16)xyzw.y), z((float16)xyzw.z), w((float16)xyzw.w) {}

		float16& operator[](u32 index) { return (&x)[index]; }
		const float16& operator[](u32 index) const { return (&x)[index]; }

		compile_const u64 count = 4;
		compile_const u64 capacity = 4;
		using ValueType = float16;
	};

	inline Vec4h Min(const Vec4h& lh, const Vec4h& rh) { return Vec4h(Min(lh.x, rh.x), Min(lh.y, rh.y), Min(lh.z, rh.z), Min(lh.w, rh.w)); }
	inline Vec4h Max(const Vec4h& lh, const Vec4h& rh) { return Vec4h(Max(lh.x, rh.x), Max(lh.y, rh.y), Max(lh.z, rh.z), Max(lh.w, rh.w)); }

	struct Mat4x4f {
		Vec4f r0 = Vec4f(1.f, 0.f, 0.f, 0.f);
		Vec4f r1 = Vec4f(0.f, 1.f, 0.f, 0.f);
		Vec4f r2 = Vec4f(0.f, 0.f, 1.f, 0.f);
		Vec4f r3 = Vec4f(0.f, 0.f, 0.f, 1.f);
		
		Vec4f& operator[](u32 index) { return (&r0)[index]; }
		const Vec4f& operator[](u32 index) const { return (&r0)[index]; }

		compile_const u32 element_count = 4;
	};

	inline Vec4f operator*(const Mat4x4f& m, const Vec4f& v) {
		Vec4f result;
		result[0] = Dot(m[0], v);
		result[1] = Dot(m[1], v);
		result[2] = Dot(m[2], v);
		result[3] = Dot(m[3], v);
		return result;
	};

	inline Vec4f operator*(const Vec4f& v, const Mat4x4f& m) {
		return (m[0] * v[0]) + (m[1] * v[1]) + (m[2] * v[2]) + (m[3] * v[3]);
	};

	inline Mat4x4f operator*(const Mat4x4f& lh, const Mat4x4f& rh) {
		Mat4x4f result;
		result[0] = (rh[0] * lh[0][0]) + (rh[1] * lh[0][1]) + (rh[2] * lh[0][2]) + (rh[3] * lh[0][3]);
		result[1] = (rh[0] * lh[1][0]) + (rh[1] * lh[1][1]) + (rh[2] * lh[1][2]) + (rh[3] * lh[1][3]);
		result[2] = (rh[0] * lh[2][0]) + (rh[1] * lh[2][1]) + (rh[2] * lh[2][2]) + (rh[3] * lh[2][3]);
		result[3] = (rh[0] * lh[3][0]) + (rh[1] * lh[3][1]) + (rh[2] * lh[3][2]) + (rh[3] * lh[3][3]);
		return result;
	};

	inline Mat4x4f Transpose(const Mat4x4f& m) {
		Mat4x4f result;
		result[0][0] = m[0][0];
		result[0][1] = m[1][0];
		result[0][2] = m[2][0];
		result[0][3] = m[3][0];
		result[1][0] = m[0][1];
		result[1][1] = m[1][1];
		result[1][2] = m[2][1];
		result[1][3] = m[3][1];
		result[2][0] = m[0][2];
		result[2][1] = m[1][2];
		result[2][2] = m[2][2];
		result[2][3] = m[3][2];
		result[3][0] = m[0][3];
		result[3][1] = m[1][3];
		result[3][2] = m[2][3];
		result[3][3] = m[3][3];
		return result;
	};

	struct Mat3x4f {
		Vec4f r0 = Vec4f(1.f, 0.f, 0.f, 0.f);
		Vec4f r1 = Vec4f(0.f, 1.f, 0.f, 0.f);
		Vec4f r2 = Vec4f(0.f, 0.f, 1.f, 0.f);
		
		Vec4f& operator[](u32 index) { return (&r0)[index]; }
		const Vec4f& operator[](u32 index) const { return (&r0)[index]; }

		compile_const u32 element_count = 3;
	};

	inline Vec3f operator*(const Mat3x4f& m, const Vec4f& v) {
		Vec3f result;
		result[0] = Dot(m[0], v);
		result[1] = Dot(m[1], v);
		result[2] = Dot(m[2], v);
		return result;
	};

	inline Vec4f operator*(const Vec3f& v, const Mat3x4f& m) {
		return (m[0] * v[0]) + (m[1] * v[1]) + (m[2] * v[2]) + (m[3] * v[3]);
	};

	inline Mat3x4f operator*(const Mat3x4f& lh, const Mat3x4f& rh) {
		Mat3x4f result;
		result[0] = (rh[0] * lh[0][0]) + (rh[1] * lh[0][1]) + (rh[2] * lh[0][2]) + (rh[3] * lh[0][3]);
		result[1] = (rh[0] * lh[1][0]) + (rh[1] * lh[1][1]) + (rh[2] * lh[1][2]) + (rh[3] * lh[1][3]);
		result[2] = (rh[0] * lh[2][0]) + (rh[1] * lh[2][1]) + (rh[2] * lh[2][2]) + (rh[3] * lh[2][3]);
		result[3] = (rh[0] * lh[3][0]) + (rh[1] * lh[3][1]) + (rh[2] * lh[3][2]) + (rh[3] * lh[3][3]);
		return result;
	};

	struct Mat3x3f {
		Vec3f r0 = Vec3f(1.f, 0.f, 0.f);
		Vec3f r1 = Vec3f(0.f, 1.f, 0.f);
		Vec3f r2 = Vec3f(0.f, 0.f, 1.f);
		
		Vec3f& operator[](u32 index) { return (&r0)[index]; }
		const Vec3f& operator[](u32 index) const { return (&r0)[index]; }

		compile_const u32 element_count = 3;
	};

	inline Vec3f operator*(const Mat3x3f& m, const Vec3f& v) {
		Vec3f result;
		result[0] = Dot(m[0], v);
		result[1] = Dot(m[1], v);
		result[2] = Dot(m[2], v);
		return result;
	};

	inline Vec3f operator*(const Vec3f& v, const Mat3x3f& m) {
		return (m[0] * v[0]) + (m[1] * v[1]) + (m[2] * v[2]);
	};

	inline Mat3x3f operator*(const Mat3x3f& lh, const Mat3x3f& rh) {
		Mat3x3f result;
		result[0] = (rh[0] * lh[0][0]) + (rh[1] * lh[0][1]) + (rh[2] * lh[0][2]);
		result[1] = (rh[0] * lh[1][0]) + (rh[1] * lh[1][1]) + (rh[2] * lh[1][2]);
		result[2] = (rh[0] * lh[2][0]) + (rh[1] * lh[2][1]) + (rh[2] * lh[2][2]);
		return result;
	};

	inline Mat3x3f Transpose(const Mat3x3f& m) {
		Mat3x3f result;
		result[0][0] = m[0][0];
		result[0][1] = m[1][0];
		result[0][2] = m[2][0];
		result[1][0] = m[0][1];
		result[1][1] = m[1][1];
		result[1][2] = m[2][1];
		result[2][0] = m[0][2];
		result[2][1] = m[1][2];
		result[2][2] = m[2][2];
		return result;
	};

} // namespace Math

