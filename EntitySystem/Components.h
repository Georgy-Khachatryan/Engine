#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMath.h"

// Transform components represent model_to_world or view_to_world translation (world space position), rotation, and scale.
NOTES() struct PositionComponent { float3 position = {};  };
NOTES() struct RotationComponent { quat   rotation = {};  };
NOTES() struct ScaleComponent    { float  scale    = 1.f; };

NOTES() struct GuidComponent { u64 guid = 0; };
NOTES() struct NameComponent { String name;  };

NOTES(Meta::ComponentQuery{})
struct GuidQuery {
	GuidComponent* guid = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct GuidNameQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
};
