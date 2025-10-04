#pragma once
#include "Basic.h"

#include <math.h>

namespace Math {

	struct Vec2u32 {
		u32 x; u32 y;

		Vec2u32() : x(0), y(0) {}
		Vec2u32(u32 x) : x(x), y(x) {}
		Vec2u32(u32 x, u32 y) : x(x), y(y) {}
		Vec2u32(const Vec2u32& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit Vec2u32(const T& xy) : x((u32)xy.x), y((u32)xy.y) {}

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

		u32& operator[](u32 index) { return (&x)[index]; }
		const u32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u32 element_count = 2;
	};

	struct Vec3u32 {
		union {
			struct { u32 x; u32 y; u32 z; };
			Vec2u32 xy;
		};

		Vec3u32() : x(0), y(0), z(0) {}
		Vec3u32(u32 x) : x(x), y(x), z(x) {}
		Vec3u32(u32 x, u32 y, u32 z) : x(x), y(y), z(z) {}
		Vec3u32(const Vec2u32& xy, u32 z) : x(xy.x), y(xy.y), z(z) {}
		Vec3u32(const Vec3u32& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit Vec3u32(const T& xyz) : x((u32)xyz.x), y((u32)xyz.y), z((u32)xyz.z) {}

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

		u32& operator[](u32 index) { return (&x)[index]; }
		const u32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u32 element_count = 3;
	};

	struct Vec4u32 {
		union {
			struct { u32 x; u32 y; u32 z; u32 w; };
			struct { Vec2u32 xy; Vec2u32 zw; };
			Vec3u32 xyz;
		};

		Vec4u32() : x(0), y(0), z(0), w(0) {}
		Vec4u32(u32 x) : x(x), y(x), z(x), w(x) {}
		Vec4u32(u32 x, u32 y, u32 z, u32 w) : x(x), y(y), z(z), w(w) {}
		Vec4u32(const Vec2u32& xy, u32 z, u32 w) : x(xy.x), y(xy.y), z(z), w(w) {}
		Vec4u32(const Vec2u32& xy, const Vec2u32& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		Vec4u32(const Vec3u32& xyz, u32 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		Vec4u32(const Vec4u32& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit Vec4u32(const T& xyzw) : x((u32)xyzw.x), y((u32)xyzw.y), z((u32)xyzw.z), w((u32)xyzw.w) {}

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

		u32& operator[](u32 index) { return (&x)[index]; }
		const u32& operator[](u32 index) const { return (&x)[index]; }

		compile_const u32 element_count = 4;
	};

	struct Vec2f {
		float x; float y;

		Vec2f() : x(0), y(0) {}
		Vec2f(float x) : x(x), y(x) {}
		Vec2f(float x, float y) : x(x), y(y) {}
		Vec2f(const Vec2f& xy) : x(xy.x), y(xy.y) {}

		template<typename T> explicit Vec2f(const T& xy) : x((float)xy.x), y((float)xy.y) {}

		Vec2f operator+(const Vec2f& other) const { return Vec2f(x + other.x, y + other.y); }
		Vec2f operator+(float other) const { return Vec2f(x + other, y + other); }

		Vec2f operator-(const Vec2f& other) const { return Vec2f(x - other.x, y - other.y); }
		Vec2f operator-(float other) const { return Vec2f(x - other, y - other); }

		Vec2f operator*(const Vec2f& other) const { return Vec2f(x * other.x, y * other.y); }
		Vec2f operator*(float other) const { return Vec2f(x * other, y * other); }

		Vec2f operator/(const Vec2f& other) const { return Vec2f(x / other.x, y / other.y); }
		Vec2f operator/(float other) const { return Vec2f(x / other, y / other); }

		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }

		compile_const u32 element_count = 2;
	};

	inline float Cross(const Vec2f& lh, const Vec2f& rh) { return lh.x * rh.y - lh.y * rh.x; }
	inline float Dot(const Vec2f& lh, const Vec2f& rh) { return lh.x * rh.x + lh.y * rh.y; }
	inline float LengthSquare(const Vec2f& v) { return Dot(v, v); }
	inline float Length(const Vec2f& v) { return sqrtf(Dot(v, v)); }

	struct Vec3f {
		union {
			struct { float x; float y; float z; };
			Vec2f xy;
		};

		Vec3f() : x(0), y(0), z(0) {}
		Vec3f(float x) : x(x), y(x), z(x) {}
		Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}
		Vec3f(const Vec2f& xy, float z) : x(xy.x), y(xy.y), z(z) {}
		Vec3f(const Vec3f& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}

		template<typename T> explicit Vec3f(const T& xyz) : x((float)xyz.x), y((float)xyz.y), z((float)xyz.z) {}

		Vec3f operator+(const Vec3f& other) const { return Vec3f(x + other.x, y + other.y, z + other.z); }
		Vec3f operator+(float other) const { return Vec3f(x + other, y + other, z + other); }

		Vec3f operator-(const Vec3f& other) const { return Vec3f(x - other.x, y - other.y, z - other.z); }
		Vec3f operator-(float other) const { return Vec3f(x - other, y - other, z - other); }

		Vec3f operator*(const Vec3f& other) const { return Vec3f(x * other.x, y * other.y, z * other.z); }
		Vec3f operator*(float other) const { return Vec3f(x * other, y * other, z * other); }

		Vec3f operator/(const Vec3f& other) const { return Vec3f(x / other.x, y / other.y, z / other.z); }
		Vec3f operator/(float other) const { return Vec3f(x / other, y / other, z / other); }

		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }

		compile_const u32 element_count = 3;
	};

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

	struct Vec4f {
		union {
			struct { float x; float y; float z; float w; };
			struct { Vec2f xy; Vec2f zw; };
			Vec3f xyz;
		};

		Vec4f() : x(0), y(0), z(0), w(0) {}
		Vec4f(float x) : x(x), y(x), z(x), w(x) {}
		Vec4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		Vec4f(const Vec2f& xy, float z, float w) : x(xy.x), y(xy.y), z(z), w(w) {}
		Vec4f(const Vec2f& xy, const Vec2f& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
		Vec4f(const Vec3f& xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
		Vec4f(const Vec4f& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}

		template<typename T> explicit Vec4f(const T& xyzw) : x((float)xyzw.x), y((float)xyzw.y), z((float)xyzw.z), w((float)xyzw.w) {}

		Vec4f operator+(const Vec4f& other) const { return Vec4f(x + other.x, y + other.y, z + other.z, w + other.w); }
		Vec4f operator+(float other) const { return Vec4f(x + other, y + other, z + other, w + other); }

		Vec4f operator-(const Vec4f& other) const { return Vec4f(x - other.x, y - other.y, z - other.z, w - other.w); }
		Vec4f operator-(float other) const { return Vec4f(x - other, y - other, z - other, w - other); }

		Vec4f operator*(const Vec4f& other) const { return Vec4f(x * other.x, y * other.y, z * other.z, w * other.w); }
		Vec4f operator*(float other) const { return Vec4f(x * other, y * other, z * other, w * other); }

		Vec4f operator/(const Vec4f& other) const { return Vec4f(x / other.x, y / other.y, z / other.z, w / other.w); }
		Vec4f operator/(float other) const { return Vec4f(x / other, y / other, z / other, w / other); }

		float& operator[](u32 index) { return (&x)[index]; }
		const float& operator[](u32 index) const { return (&x)[index]; }

		compile_const u32 element_count = 4;
	};

	inline float Dot(const Vec4f& lh, const Vec4f& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z + lh.w * rh.w; }
	inline float LengthSquare(const Vec4f& v) { return Dot(v, v); }
	inline float Length(const Vec4f& v) { return sqrtf(Dot(v, v)); }

	struct Mat4x4f {
		Vec4f rows[4];

		Vec4f& operator[](u32 index) { return rows[index]; }
		const Vec4f& operator[](u32 index) const { return rows[index]; }

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
		Vec4f result;
		result = result + (m[0] * v[0]);
		result = result + (m[1] * v[1]);
		result = result + (m[2] * v[2]);
		result = result + (m[3] * v[3]);
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
		Vec4f rows[3];

		Vec4f& operator[](u32 index) { return rows[index]; }
		const Vec4f& operator[](u32 index) const { return rows[index]; }

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
		Vec4f result;
		result = result + (m[0] * v[0]);
		result = result + (m[1] * v[1]);
		result = result + (m[2] * v[2]);
		result = result + (m[3] * v[3]);
		return result;
	};

	struct Mat3x3f {
		Vec3f rows[3];

		Vec3f& operator[](u32 index) { return rows[index]; }
		const Vec3f& operator[](u32 index) const { return rows[index]; }

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
		Vec3f result;
		result = result + (m[0] * v[0]);
		result = result + (m[1] * v[1]);
		result = result + (m[2] * v[2]);
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

