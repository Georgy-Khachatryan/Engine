#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMath.h"

NOTES() struct NameComponent { String name; };

// Transform components represent model_to_world or view_to_world translation (world space position), rotation, and scale.
NOTES() struct PositionComponent { float3 position = {};  };
NOTES() struct RotationComponent { quat   rotation = {};  };
NOTES() struct ScaleComponent    { float  scale    = 1.f; };


NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct GpuTransform {
	float3 position;
	float scale;
	quat rotation;
};

