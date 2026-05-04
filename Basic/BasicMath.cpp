#include "BasicMath.h"

bool Math::IsPerspectiveMatrix(const float4& coefficients)  { return coefficients.w == 0.f; }
bool Math::IsOrthographicMatrix(const float4& coefficients) { return coefficients.w != 0.f; }

// 
// Perspective ViewToClip/ClipToView:
// 
// ViewToClip    ClipToView
//  x 0 0 0      1/x 0  0  0
//  0 y 0 0      0  1/y 0  0
//  0 0 0 z      0   0  0  1
//  0 0 1 0      0   0 1/z 0
// 
float4 Math::PerspectiveViewToClip(float vertical_fov, float2 viewport_size, float near_depth) {
	float inv_tan_half_fov = 1.f / tanf(vertical_fov * 0.5f);
	float aspect_ratio     = Max(viewport_size.y, 1.f) / Max(viewport_size.x, 1.f);
	
	float4 view_to_clip_coef;
	view_to_clip_coef.x = inv_tan_half_fov * aspect_ratio;
	view_to_clip_coef.y = -inv_tan_half_fov;
	view_to_clip_coef.z = near_depth;
	view_to_clip_coef.w = 0.f;
	
	return view_to_clip_coef;
}

// 
// Orthographic ViewToClip/ClipToView:
// 
// ViewToClip     ClipToView
//  x 0 0 0      1/x 0  0    0
//  0 y 0 0      0  1/y 0    0
//  0 0 z 1      0   0 1/z -1/z
//  0 0 0 1      0   0  0    1
// 
float4 Math::OrthographicViewToClip(float2 size, float far_depth) {
	float4 view_to_clip_coef;
	view_to_clip_coef.x = 2.f / size.x;
	view_to_clip_coef.y = -2.f / size.y;
	view_to_clip_coef.z = -1.f / far_depth;
	view_to_clip_coef.w = 1.f;
	
	return view_to_clip_coef;
}

float4 Math::ViewToClipInverse(const float4& view_to_clip_coef) {
	float4 clip_to_view_coef;
	if (Math::IsPerspectiveMatrix(view_to_clip_coef)) {
		clip_to_view_coef.xy = float2(1.f) / view_to_clip_coef.xy;
		clip_to_view_coef.z  = 1.f / view_to_clip_coef.z;
		clip_to_view_coef.w  = 0.f;
	} else {
		clip_to_view_coef.xyz = float3(1.f) / view_to_clip_coef.xyz;
		clip_to_view_coef.w   = 1.f;
	}
	return clip_to_view_coef;
}

float4 Math::TransformViewToClipSpace(const float3& view_space_position, const float4& view_to_clip_coef) {
	float4 result;
	result.xy = view_space_position.xy * view_to_clip_coef.xy;
	
	if (Math::IsPerspectiveMatrix(view_to_clip_coef)) {
		result.z = view_to_clip_coef.z;
		result.w = view_space_position.z;
	} else {
		result.z = view_space_position.z * view_to_clip_coef.z + 1.f;
		result.w = 1.f;
	}
	
	return result;
}


Math::RayInfo Math::RayInfoFromNdc(float2 ndc, const float4& clip_to_view_coef) {
	Math::RayInfo result;
	
	if (Math::IsPerspectiveMatrix(clip_to_view_coef)) {
		result.origin    = float3(0.f, 0.f, 0.f);
		result.direction = Math::Normalize(float3(ndc * clip_to_view_coef.xy, 1.f));
	} else {
		result.origin    = float3(ndc * clip_to_view_coef.xy, 0.f);
		result.direction = float3(0.f, 0.f, 1.f);
	}
	
	return result;
}

Math::RayInfo Math::RayInfoFromScreenUv(float2 uv, const float4& clip_to_view_coef) {
	return Math::RayInfoFromNdc(Math::ScreenUvToNdc(uv), clip_to_view_coef);
}

Math::RayInfo Math::TransformRayViewToWorld(const Math::RayInfo& view_space_ray, const float3& world_space_position, const quat& view_to_world_rotation) {
	Math::RayInfo world_space_ray;
	world_space_ray.direction = view_to_world_rotation * view_space_ray.direction;
	world_space_ray.origin    = view_to_world_rotation * view_space_ray.origin + world_space_position;
	
	return world_space_ray;
}


always_inline static Math::RayHitResult RayCylinderCapIntersect(const Math::RayInfo& ray, float t, const float3& p, float radius_0, float radius_1) {
	float distance_square = Math::LengthSquare(ray.origin + ray.direction * t - p);
	bool is_in_range = (t > 0.f) && (radius_0 * radius_0 < distance_square) && (distance_square < radius_1 * radius_1);
	
	return { t, is_in_range };
}

always_inline static Math::RayHitResult RayCylinderBodyIntersect(const Math::RayInfo& ray, float r, float d, float baba, float baoc, float bard, float k1, float k2, float sign) {
	float kr = d - r * r * baba;
	float h = k1 * k1 - k2 * kr;
	if (h < 0.f) return {};
	
	float t = (sqrtf(h) * sign - k1) / k2;
	float y = baoc + t * bard;
	
	bool is_in_range = (y > 0.f) && (y < baba) && (t > 0.f);
	return { t, is_in_range };
}

// Cylinder defined by extremes a and b, outer radius_1 and optional inner radius_1 that makes cylinder a tube.
// https://www.shadertoy.com/view/4lcSRn see license in THIRD_PARTY_LICENSES.md
Math::RayHitResult Math::RayCylinderIntersect(const Math::RayInfo& ray, const float3& a, const float3& b, float radius_0, float radius_1) {
	auto ba = b - a;
	auto oc = ray.origin - a;
	
	float baba = Math::Dot(ba, ba);
	float bard = Math::Dot(ba, ray.direction);
	float baoc = Math::Dot(ba, oc);
	
	float k2 = baba - bard * bard;
	float k1 = baba * Math::Dot(oc, ray.direction) - baoc * bard;
	
	Math::RayHitResult hit_result;
	
	// Caps:
	compile_const float eps = 1.f / (1024.f * 1024.f);
	if (fabsf(bard) >= eps) {
		float ta = -Math::Dot(ray.origin - a, ba) / bard;
		float tb = ta + baba / bard;
		
		auto cap_hit_a = RayCylinderCapIntersect(ray, ta, a, radius_0, radius_1);
		if (cap_hit_a.hit) hit_result.AddHit(ta);
		
		auto cap_hit_b = RayCylinderCapIntersect(ray, tb, b, radius_0, radius_1);
		if (cap_hit_b.hit) hit_result.AddHit(tb);
	}
	
	float d = baba * Math::Dot(oc, oc) - baoc * baoc;
	
	auto outer_hit = RayCylinderBodyIntersect(ray, radius_1, d, baba, baoc, bard, k1, k2, -1.f);
	if (outer_hit.hit) hit_result.AddHit(outer_hit.hit_distance);
	
	if (radius_0 > 0.f) {
		auto inner_hit = RayCylinderBodyIntersect(ray, radius_0, d, baba, baoc, bard, k1, k2, +1.f);
		if (inner_hit.hit) hit_result.AddHit(inner_hit.hit_distance);
	}
	
	return hit_result;
}

// Ray-Box intersection, by convertig the ray to the local space of the box.
// https://www.shadertoy.com/view/ld23DV see license in THIRD_PARTY_LICENSES.md
Math::RayHitResult Math::RayBoxIntersect(const Math::RayInfo& ray, const float3& position, const quat& rotation, const float3& half_extent) {
	auto world_to_box_rotation = Math::Conjugate(rotation);
	auto local_ray = Math::TransformRayViewToWorld(ray, -(world_to_box_rotation * position), world_to_box_rotation);
	
	auto m = float3(1.f, 1.f, 1.f) / local_ray.direction;
	auto n = m * local_ray.origin;
	auto k = float3(fabsf(m.x), fabsf(m.y), fabsf(m.z)) * half_extent;
	
	auto t0 = -n - k;
	auto t1 = -n + k;
	
	float t_n = Max(Max(t0.x, t0.y), t0.z);
	float t_f = Min(Min(t1.x, t1.y), t1.z);
	
	bool is_in_range = (t_n <= t_f) && (t_f >= 0.f);
	return { t_n >= 0.f ? t_n : t_f, is_in_range };
}

// The plane is defined by Math::Dot(normal, p) + distance = 0. Distance can be computed as -Math::Dot(normal, point_on_plane).
Math::RayHitResult Math::RayPlaneIntersect(const Math::RayInfo& ray, const float3& normal, float distance) {
	float denominator = Math::Dot(normal, ray.direction);
	if (denominator == 0.f) return {};
	
	float hit_distance = (Math::Dot(normal, ray.origin) + distance) / -denominator;
	return { hit_distance, hit_distance >= 0.f };
}


// Tom Duff, James Burgess, Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, and Ryusuke Villemin.
// 2017. Building an Orthonormal Basis, Revisited. https://jcgt.org/published/0006/01/01/
float3x3 Math::BuildOrthonormalBasis(const float3& normal) {
	float sign = normal.z < 0.f ? -1.f : +1.f;
	
	float a = -1.f / (sign + normal.z);
	float b = normal.x * normal.y * a;
	
	float3x3 result;
	result.r0 = float3(1.f + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
	result.r1 = float3(b, sign + normal.y * normal.y * a, -normal.y);
	result.r2 = normal;
	
	return result;
}

quat Math::AxisAngleToQuat(const float3& axis, float angle) {
	float half_angle = angle * 0.5f;
	float sin_half_angle = sinf(half_angle);
	float cos_half_angle = cosf(half_angle);
	return quat(axis * sin_half_angle, cos_half_angle);
}

quat Math::AxisAxisToQuat(const float3& axis_0, const float3& axis_1) {
	float quat_w = Math::Dot(axis_0, axis_1) + 1.f;
	compile_const float eps = 1.f / (1024.f * 1024.f);
	return fabsf(quat_w) > eps ? Math::Normalize(quat(Math::Cross(axis_0, axis_1), quat_w)) : quat(Math::BuildOrthonormalBasis(axis_0).r0, 0.f);
}

float3x3 Math::QuatToRotationMatrix(const quat& q) {
	float3x3 result;
	result.r0 = float3(1.f - 2.f * (q.y * q.y + q.z * q.z),       2.f * (q.x * q.y - q.z * q.w),       2.f * (q.x * q.z + q.y * q.w));
	result.r1 = float3(      2.f * (q.x * q.y + q.z * q.w), 1.f - 2.f * (q.x * q.x + q.z * q.z),       2.f * (q.y * q.z - q.x * q.w));
	result.r2 = float3(      2.f * (q.x * q.z - q.y * q.w),       2.f * (q.y * q.z + q.x * q.w), 1.f - 2.f * (q.x * q.x + q.y * q.y));
	return result;
}

// Evandro Bernardes, Stephane Viollet. 2022. Quaternion to Euler angles conversion: a direct, general and computationally efficient method.
float3 Math::QuatToEulerXyzAngles(const quat& q) {
	float a = q.w - q.y;
	float b = q.x + q.z;
	float c = q.y + q.w;
	float d = q.z - q.x;
	
	float theta_0 = 0.f;
	float theta_1 = acosf(((a * a + b * b) / (a * a + b * b + c * c + d * d)) * 2.f - 1.f);
	float theta_2 = 0.f;
	
	float theta_sum  = atan2f(b, a);
	float theta_diff = atan2f(-d, c);
	
	compile_const float eps = (1.f / 128.f) * Math::degrees_to_radians;
	bool is_safe_1 = fabsf(theta_1) >= eps;
	bool is_safe_2 = fabsf(theta_1 - Math::PI) >= eps;
	
	if (is_safe_1 == false) {
		theta_0 = theta_sum * 2.f;
	} else if (is_safe_2 == false) {
		theta_0 = theta_diff * 2.f;
	} else {
		theta_0 = theta_sum + theta_diff;
		theta_2 = theta_sum - theta_diff;
	}
	
	theta_1 -= Math::PI * 0.5f;
	
	return float3(theta_0, theta_1, theta_2);
}

// https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
quat Math::EulerXyzAnglesToQuat(const float3& e) {
	float cx = cosf(e.x * 0.5f);
	float sx = sinf(e.x * 0.5f);
	float cy = cosf(e.y * 0.5f);
	float sy = sinf(e.y * 0.5f);
	float cz = cosf(e.z * 0.5f);
	float sz = sinf(e.z * 0.5f);
	
	quat q;
	q.x = sx * cy * cz - cx * sy * sz;
	q.y = cx * sy * cz + sx * cy * sz;
	q.z = cx * cy * sz - sx * sy * cz;
	q.w = cx * cy * cz + sx * sy * sz;
	
	return q;
}


s16x4 Math::EncodeR16G16B16A16_SNORM(const float4& value) {
	auto scaled_value_float32 = _mm_mul_ps(_mm_loadu_ps(&value.x), _mm_set_ps1((float)s16_max));
	auto scaled_value_s32 = _mm_cvtps_epi32(scaled_value_float32);
	auto scaled_value_s16 = _mm_packs_epi32(scaled_value_s32, scaled_value_s32);
	return s16x4(_mm_extract_epi16(scaled_value_s16, 0), _mm_extract_epi16(scaled_value_s16, 1), _mm_extract_epi16(scaled_value_s16, 2), _mm_extract_epi16(scaled_value_s16, 3));
}

s16x2 Math::EncodeR16G16_SNORM(const float2& value) {
	auto scaled_value_float32 = _mm_mul_ps(_mm_setr_ps(value.x, value.y, 0.f, 0.f), _mm_set_ps1((float)s16_max));
	auto scaled_value_s32 = _mm_cvtps_epi32(scaled_value_float32);
	auto scaled_value_s16 = _mm_packs_epi32(scaled_value_s32, scaled_value_s32);
	return s16x2(_mm_extract_epi16(scaled_value_s16, 0), _mm_extract_epi16(scaled_value_s16, 1));
}

float16x4 Math::EncodeR16G16B16A16_FLOAT(const float4& value) {
	auto result = _mm_cvtps_ph(_mm_loadu_ps(&value.x), 0);
	return float16x4(_mm_extract_epi16(result, 0), _mm_extract_epi16(result, 1), _mm_extract_epi16(result, 2), _mm_extract_epi16(result, 3));
}

float16x2 Math::EncodeR16G16_FLOAT(const float2& value) {
	auto result = _mm_cvtps_ph(_mm_setr_ps(value.x, value.y, 0.f, 0.f), 0);
	return float16x2(_mm_extract_epi16(result, 0), _mm_extract_epi16(result, 1));
}

float16 Math::EncodeR16_FLOAT(float value) {
	auto result = _mm_cvtps_ph(_mm_setr_ps(value, 0.f, 0.f, 0.f), 0);
	return _mm_extract_epi16(result, 0);
}

float Math::DecodeR16_FLOAT(float16 value) {
	auto result = _mm_cvtph_ps(_mm_setr_epi32(value, 0, 0, 0));
	return _mm_cvtss_f32(result);
}

u32 Math::EncodeR10G10B10(const float3& value) {
	auto scaled_value_float32 = _mm_mul_ps(_mm_setr_ps(value.x, value.y, value.z, 0.f), _mm_set_ps1(1023.f));
	auto scaled_value_u32 = _mm_cvtps_epi32(scaled_value_float32);
	return (_mm_extract_epi32(scaled_value_u32, 0) << 0) | (_mm_extract_epi32(scaled_value_u32, 1) << 10) | (_mm_extract_epi32(scaled_value_u32, 2) << 20);
}

float3 Math::DecodeR10G10B10(u32 encoded) {
	auto scaled_value_u32 = _mm_and_si128(_mm_setr_epi32(encoded >> 0, encoded >> 10, encoded >> 20, 0), _mm_setr_epi32(0x3FF, 0x3FF, 0x3FF, 0));
	auto scaled_value_float32 = _mm_mul_ps(_mm_cvtepi32_ps(scaled_value_u32), _mm_set_ps1(1.f / 1023.f));
	alignas(16) float4 result;
	_mm_store_ps(&result.x, scaled_value_float32);
	return result.xyz;
}

always_inline static float EncodeSrgbChannel(float x) { return (x < 0.04045f ? (x / 12.92f) : powf((x + 0.055f) / 1.055f, 2.4f)); }
always_inline static float DecodeSrgbChannel(float x) { return (x < 0.0031308f ? 12.92f * x : (1.055f * powf(x, 1.f / 2.4f) - 0.055f)); }

float3 Math::DecodeSRGB(const float3& x) { return float3(DecodeSrgbChannel(x.x), DecodeSrgbChannel(x.y), DecodeSrgbChannel(x.z)); }
float3 Math::EncodeSRGB(const float3& x) { return float3(EncodeSrgbChannel(x.x), EncodeSrgbChannel(x.y), EncodeSrgbChannel(x.z)); }
float4 Math::DecodeSRGB(const float4& x) { return float4(DecodeSrgbChannel(x.x), DecodeSrgbChannel(x.y), DecodeSrgbChannel(x.z), x.w); }
float4 Math::EncodeSRGB(const float4& x) { return float4(EncodeSrgbChannel(x.x), EncodeSrgbChannel(x.y), EncodeSrgbChannel(x.z), x.w); }


// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
always_inline static float2 OctahedralWrap(const float2& value) {
	float2 result;
	result.x = (1.f - fabsf(value.y)) * (value.x >= 0.f ? 1.f : -1.f);
	result.y = (1.f - fabsf(value.x)) * (value.y >= 0.f ? 1.f : -1.f);
	return result;
};

float2 Math::EncodeOctahedralMap(const float3& value) {
	float2 result = value.xy * (1.f / (fabsf(value.x) + fabsf(value.y) + fabsf(value.z)));
	return value.z >= 0.f ? result : OctahedralWrap(result);
}

// Optimized octahedron decoding by Rune Stubbe.
float3 Math::DecodeOctahedralMap(const float2& value) {
	float3 result = float3(value.x, value.y, 1.f - fabsf(value.x) - fabsf(value.y));
	
	float t = Math::Min(Math::Max(-result.z, 0.f), 1.f);
	result.x += result.x >= 0.f ? -t : t;
	result.y += result.y >= 0.f ? -t : t;
	
	return Math::Normalize(result);
}


// https://en.wikipedia.org/wiki/Halton_sequence
float Math::HaltonSequence(u32 index, u32 base) {
	float f = 1.f;
	float r = 0.f;
	
	while (index != 0) {
		f /= (float)base;
		r += f * (index % base);
		index /= base;
	}
	
	return r;
}
