#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "EntitySystem/Components.h"
#include "EntitySystem/EntitySystem.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/MeshAsset.h"
#include "Renderer/RendererEntities.h"
#include "Renderer/TextureAsset.h"
#include "WorldAsset.h"

struct RecordContext;
struct GpuComponentUploadBuffer;

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct WorldEntityType {
	ECS::Component<GuidComponent> guid;
	
	ECS::Component<CameraEntityGUID> camera_entity;
	ECS::Component<LightEntityGUID>  global_light_entity;
	ECS::Component<RendererWorld>    renderer_world;
	
	ECS::Component<CloudSettings>        cloud_settings;
	ECS::Component<FogSettings>          fog_settings;
	ECS::Component<LightingSettings>     lighting_settings;
	ECS::Component<ExposureSettings>     exposure_settings;
	ECS::Component<ToneMappingSettings>  tone_mapping_settings;
	ECS::Component<AntiAliasingSettings> anti_aliasing_settings;
	
	NOTES(VirtualResourceID::SceneConstants)
	ECS::GpuComponent<SceneConstants> gpu_scene_constants;
};

NOTES()
struct MeshEntityMaterialTable {
	FixedCountArray<MaterialAssetGUID, MeshAssetMaterialTable::max_materials> materials;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct MeshEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<ScaleComponent>    scale;
	
	ECS::Component<MeshAssetGUID> mesh_asset;
	ECS::Component<MeshEntityMaterialTable> material_table;
	
	NOTES(VirtualResourceID::MeshEntityGpuTransform)
	ECS::GpuComponent<GpuTransform> gpu_transform;
	
	NOTES(VirtualResourceID::MeshEntityPrevGpuTransform)
	ECS::GpuComponent<GpuTransform> prev_gpu_transform;
	
	NOTES(VirtualResourceID::MeshEntityAliveMask)
	ECS::GpuMaskComponent<AliveEntityMask> alive_mask;
	
	NOTES(VirtualResourceID::GpuMeshEntityData)
	ECS::GpuComponent<GpuMeshEntityData> gpu_mesh_entity_data;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct CameraEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<CameraComponent>   camera;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct LightEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<LightComponent>    light;
	
	NOTES(VirtualResourceID::LightEntityAliveMask)
	ECS::GpuMaskComponent<AliveEntityMask> alive_mask;
	
	NOTES(VirtualResourceID::GpuLightEntityData)
	ECS::GpuComponent<GpuLightEntityData> gpu_light_entity_data;
};

NOTES()
struct CloudVolume {
	TextureAssetGUID sdf_texture;
};

NOTES(Meta::EntityType{ 256 }, Meta::ComponentQuery{})
struct CloudVolumeEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<ScaleComponent>    scale;
	
	ECS::Component<CloudVolume> cloud_volume;
	
	NOTES(VirtualResourceID::CloudVolumeAliveMask)
	ECS::GpuMaskComponent<AliveEntityMask> alive_mask;
	
	NOTES(VirtualResourceID::GpuCloudVolumeEntityData)
	ECS::GpuComponent<GpuCloudVolumeEntityData> gpu_cloud_volume_entity_data;
};

NOTES(Meta::ComponentQuery{})
struct TransformComponentQuery {
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	ScaleComponent*    scale    = nullptr;
};


void UpdateEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads);

void ReleaseEntityComponents(StackAllocator* alloc, EntitySystemBase& entity_system);
