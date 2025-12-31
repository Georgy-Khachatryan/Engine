#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicSaveLoad.h"
#include "EntitySystem.h"

NOTES() struct NameComponent { String name; };

// Transform components represent model_to_world or view_to_world translation (world space position), rotation, and scale.
NOTES() struct PositionComponent { float3 position = {};  };
NOTES() struct RotationComponent { quat   rotation = {};  };
NOTES() struct ScaleComponent    { float  scale    = 1.f; };


NOTES(Meta::EntityType{})
struct MeshEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<ScaleComponent>    scale;
	
	NOTES(VirtualResourceID::MeshEntityGpuTransform)
	ECS::GpuComponent<float3x4> gpu_transform;
};


NOTES()
enum struct CameraTransformType : u32 {
	Perspective  = 0,
	Orthographic = 1,
};

NOTES()
struct CameraComponent {
	float vertical_fov_degrees = 75.f;
	CameraTransformType transform_type = CameraTransformType::Perspective;
};

NOTES(Meta::EntityType{})
struct CameraEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<CameraComponent>   camera;
};


NOTES(Meta::ComponentQuery{})
struct GuidNameQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct PositionQuery {
	PositionComponent* position = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct RotationQuery {
	RotationComponent* rotation = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct ScaleQuery {
	ScaleComponent* scale = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct NameQuery {
	NameComponent* name = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct PositionRotationCameraQuery {
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	CameraComponent*   camera   = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct PositionRotationScaleGpuTransformQuery {
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	ScaleComponent*    scale    = nullptr;
	
	ECS::GpuComponent<float3x4> gpu_transform;
};
