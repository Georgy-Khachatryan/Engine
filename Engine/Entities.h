#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "Renderer/MeshAsset.h"
#include "Renderer/RendererEntities.h"


NOTES(Meta::CustomSaveLoad{})
struct EditorSelectionState {
	// TODO: Add support for hash table reflection and save/load.
	HashTable<u64, void> selected_entities_hash_table;
};

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct WorldEntityType {
	ECS::Component<GuidComponent> guid;
	
	ECS::Component<CameraEntityGUID> camera_entity;
	ECS::Component<RendererWorld> renderer_world;
	
	NOTES(VirtualResourceID::SceneConstants)
	ECS::GpuComponent<SceneConstants> gpu_scene_constants;
	
	ECS::Component<EditorSelectionState> selection_state;
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

NOTES(Meta::ComponentQuery{})
struct AliveEntityMaskQuery {
	ECS::GpuMaskComponent<AliveEntityMask> alive_mask;
};


struct RecordContext;
void UpdateEntityGpuComponents(StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads);

void ReleaseEntityComponents(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system);
