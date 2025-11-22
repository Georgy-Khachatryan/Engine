#include "BasicMath.h"

bool Math::IsPerspectiveMatrix(const float4& coefficients)  { return coefficients.w == 0.0; }
bool Math::IsOrthographicMatrix(const float4& coefficients) { return coefficients.w != 0.0; }

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


quat Math::AxisAngleToQuat(const float3& axis, float angle) {
	float half_angle = angle * 0.5f;
	float sin_half_angle = sinf(half_angle);
	float cos_half_angle = cosf(half_angle);
	return quat(axis * sin_half_angle, cos_half_angle);
}

float3x3 Math::QuatToRotationMatrix(const quat& q) {
	float3x3 result;
	result[0] = float3(1.f - 2.f * (q.y * q.y + q.z * q.z),       2.f * (q.x * q.y - q.z * q.w),       2.f * (q.x * q.z + q.y * q.w));
	result[1] = float3(      2.f * (q.x * q.y + q.z * q.w), 1.f - 2.f * (q.x * q.x + q.z * q.z),       2.f * (q.y * q.z - q.x * q.w));
	result[2] = float3(      2.f * (q.x * q.z - q.y * q.w),       2.f * (q.y * q.z + q.x * q.w), 1.f - 2.f * (q.x * q.x + q.y * q.y));
	return result;
}
