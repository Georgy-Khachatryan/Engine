#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Components.h"
#include "EntitySystem.h"
#include "MeshAsset.h"


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
};

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct WorldEntityType {
	ECS::Component<GuidComponent> guid;
	
	ECS::Component<CameraEntityGUID> camera_entity;
	ECS::Component<RendererWorld> renderer_world;
};


NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct MeshEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<ScaleComponent>    scale;
	
	ECS::Component<MeshAssetGUID> mesh_asset;
	
	NOTES(VirtualResourceID::MeshEntityGpuTransform)
	ECS::GpuComponent<GpuTransform> gpu_transform;
	
	NOTES(VirtualResourceID::GpuMeshEntityData)
	ECS::GpuComponent<GpuMeshEntityData> gpu_mesh_entity_data;
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
struct EntityEditorQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
	
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	ScaleComponent*    scale    = nullptr;
	
	CameraComponent* camera = nullptr;
};
