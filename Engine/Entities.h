#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "EntitySystem.h"
#include "Components.h"


NOTES()
struct CameraEntityGUID {
	u64 guid = 0;
};

NOTES(Meta::NoSaveLoad{})
struct RendererWorld {
	float2 window_size = float2(1.f, 1.f);
	float sun_elevation_degrees = 3.f;
	float meshlet_target_error_pixels = 1.f;
	
	ArrayView<struct GpuComponentUploadBuffer> gpu_uploads;
	ArrayView<struct BasicVertex>  vertices;
	ArrayView<struct BasicMeshlet> meshlets;
	ArrayView<u8>                  indices;
};

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct WorldEntityType {
	ECS::Component<GuidComponent> guid;
	
	ECS::Component<CameraEntityGUID> camera_entity;
	ECS::Component<RendererWorld> renderer_world;
};


NOTES(Meta::EntityType{})
struct MeshEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<ScaleComponent>    scale;
	
	NOTES(VirtualResourceID::MeshEntityGpuTransform)
	ECS::GpuComponent<GpuTransform> gpu_transform;
};


NOTES()
enum struct CameraTransformType : u32 {
	Perspective  = 0,
	Orthographic = 1,
};

NOTES()
struct CameraComponent {
	float vertical_fov_degrees = 75.f;
	float near_depth           = 0.1f;
	CameraTransformType transform_type = CameraTransformType::Perspective;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
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
	
	ECS::GpuComponent<GpuTransform> gpu_transform;
};
