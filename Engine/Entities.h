#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicSaveLoad.h"
#include "EntitySystem.h"

NOTES() struct NameComponent { String name; };
NOTES() struct PositionComponent { float3 position; };
NOTES() struct ScaleComponent { float scale = 1.f; };
NOTES() struct RotationComponent { quat rotation; };


NOTES(Meta::EntityType{})
struct MeshEntityType {
	ESC::Component<GuidComponent> guid;
	ESC::Component<NameComponent> name;
	
	ESC::Component<PositionComponent> position;
	ESC::Component<RotationComponent> rotation;
	ESC::Component<ScaleComponent>    scale;
};


NOTES()
enum struct CameraTransformType : u32 {
	Perspective  = 0,
	Orthographic = 1,
};
SAVE_LOAD_AS_BYTES(CameraTransformType);

NOTES()
struct CameraComponent {
	float vertical_fov_degrees = 75.f;
	CameraTransformType transform_type = CameraTransformType::Perspective;
};

NOTES(Meta::EntityType{})
struct CameraEntityType {
	ESC::Component<GuidComponent> guid;
	ESC::Component<NameComponent> name;
	
	ESC::Component<PositionComponent> position;
	ESC::Component<RotationComponent> rotation;
	ESC::Component<CameraComponent>   camera;
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
