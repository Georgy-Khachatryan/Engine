#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_ENABLE_CUSTOM_CHANGES

#define IM_ASSERT(condition) DebugAssert(condition, #condition)

#define IM_VEC2_CLASS_EXTRA\
	constexpr ImVec2(const float2& v) : x(v.x), y(v.y) {}\
	operator float2() const { return float2(x, y); }

#define IM_VEC4_CLASS_EXTRA\
	constexpr ImVec4(const float4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}\
	operator float4() const { return float4(x, y, z, w); }
