#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMath.h"

// Transform components represent model_to_world or view_to_world translation (world space position), rotation, and scale.
NOTES() struct PositionComponent { float3 position = {};  };
NOTES() struct RotationComponent { quat   rotation = {};  };
NOTES() struct ScaleComponent    { float  scale    = 1.f; };

// No SaveLoad for tooling, GUID is supposed to be tracked elsewhere.
NOTES(Meta::SaveLoadOptions{ SaveLoadFlags::SaveLoadToDisk })
struct GuidComponent {
	u64 guid = 0;
	bool operator== (GuidComponent other) const { return guid == other.guid; }
};

NOTES()
struct NameComponent {
	String name;
};

NOTES()
struct AliveEntityMask {
	u64 mask = 0;
};

NOTES()
struct AabbComponent {
	float3 min;
	float3 max;
};

NOTES()
struct HierarchyComponent {
	GuidComponent parent;
	Array<GuidComponent> children;
};

NOTES(Meta::ComponentQuery{})
struct GuidQuery {
	GuidComponent* guid = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct NameQuery {
	NameComponent* name = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct HierarchyQuery {
	HierarchyComponent* hierarchy = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct GuidNameQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct GuidHierarchyQuery {
	GuidComponent* guid = nullptr;
	HierarchyComponent* hierarchy = nullptr;
};
