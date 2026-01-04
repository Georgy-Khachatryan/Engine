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
	if (view_to_clip_coef.w == 0.f) {
		clip_to_view_coef.xy = float2(1.f) / view_to_clip_coef.xy;
		clip_to_view_coef.z  = 1.f / view_to_clip_coef.z;
		clip_to_view_coef.w  = 0.f;
	} else {
		clip_to_view_coef.xyz = float3(1.f) / view_to_clip_coef.xyz;
		clip_to_view_coef.w   = 1.f;
	}
	return clip_to_view_coef;
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


quat Math::AxisAngleToQuat(const float3& axis, float angle) {
	float half_angle = angle * 0.5f;
	float sin_half_angle = sinf(half_angle);
	float cos_half_angle = cosf(half_angle);
	return quat(axis * sin_half_angle, cos_half_angle);
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
