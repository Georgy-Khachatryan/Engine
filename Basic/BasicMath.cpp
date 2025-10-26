#include "BasicMath.h"

// 
// Perspective ViewToClip/ClipToView:
// 
// ViewToClip    ClipToView
//  x 0 0 0      1/x 0  0  0
//  0 y 0 0      0  1/y 0  0
//  0 0 0 w      0   0  0  1
//  0 0 1 0      0   0 1/w 0
// 
float4 Math::PerspectiveViewToClip(float vertical_fov, float2 viewport_size, float near_depth) {
	float inv_tan_half_fov = 1.f / tanf(vertical_fov * 0.5f);
	float aspect_ratio     = Max(viewport_size.y, 1.f) / Max(viewport_size.x, 1.f);
	
	float4 view_to_clip_coef;
	view_to_clip_coef.x = inv_tan_half_fov * aspect_ratio;
	view_to_clip_coef.y = -inv_tan_half_fov;
	view_to_clip_coef.z = 0.f;
	view_to_clip_coef.w = near_depth;
	
	return view_to_clip_coef;
}

// 
// Orthographic ViewToClip/ClipToView:
// 
// ViewToClip     ClipToView
//  x 0 0 0      1/x 0  0    0
//  0 y 0 0      0  1/y 0    0
//  0 0 z w      0   0 1/z -w/z
//  0 0 0 1      0   0  0    1
// 
float4 Math::OrthographicViewToClip(float2 size, float2 depth_range) {
	float inv_depth_size = 1.f / (depth_range.y - depth_range.x);
	
	float4 view_to_clip_coef;
	view_to_clip_coef.x = 2.f / size.x;
	view_to_clip_coef.y = -2.f / size.y;
	view_to_clip_coef.z = inv_depth_size;
	view_to_clip_coef.w = -depth_range.x * inv_depth_size;
	
	return view_to_clip_coef;
}

float4 Math::ViewToClipInverse(const float4& view_to_clip_coef) {
	float4 clip_to_view_coef;
	if (view_to_clip_coef.z == 0.f) {
		clip_to_view_coef.xy = float2(1.f) / view_to_clip_coef.xy;
		clip_to_view_coef.z  = 0.f;
		clip_to_view_coef.w  = 1.f / view_to_clip_coef.w;
	} else {
		clip_to_view_coef.xyz = float3(1.f) / view_to_clip_coef.xyz;
		clip_to_view_coef.w   = -view_to_clip_coef.w / view_to_clip_coef.z;
	}
	return clip_to_view_coef;
}
