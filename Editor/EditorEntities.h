#pragma once
#include "Basic/Basic.h"
#include "Engine/Entities.h"

NOTES(Meta::SaveLoadOptions{ SaveLoadFlags::Default | SaveLoadFlags::CustomSaveLoadCallback })
struct EditorSelectionStateComponent {
	HashTable<u64, void> selected_entities_hash_table;
	
	String search_pattern; // Not saved.
};

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct EditorSelectionStateEntity {
	ECS::Component<GuidComponent> guid;
	ECS::Component<EditorSelectionStateComponent> selection_state;
};


NOTES()
struct EditorIconCacheSettingsComponent {
	MeshAssetGUID material_icon_mesh;
};

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct EditorSettingsEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<EditorIconCacheSettingsComponent> icon_cache_settings;
};


NOTES(Meta::ComponentQuery{})
struct SharedEntityEditorQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct WorldEntityEditorQuery {
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	ScaleComponent*    scale    = nullptr;
	
	MeshAssetGUID* mesh_asset = nullptr;
	MeshEntityMaterialTable* mesh_entity_material_table = nullptr;
	
	LightEntityGUID* light_entity = nullptr;
	
	CameraComponent* camera = nullptr;
	
	LightComponent* light = nullptr;
	
	CloudSettings* cloud_settings = nullptr;
	CloudVolume*   cloud_volume   = nullptr;
	
	FogSettings* fog_settings = nullptr;
	
	RendererWorld* renderer_world = nullptr;
	
	LightingSettings*     lighting_settings      = nullptr;
	ExposureSettings*     exposure_settings      = nullptr;
	ToneMappingSettings*  tone_mapping_settings  = nullptr;
	AntiAliasingSettings* anti_aliasing_settings = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct AssetEntityEditorQuery {
	MeshAssetMaterialTable* mesh_asset_material_table = nullptr;
	
	MeshSourceData* mesh_source_data = nullptr;
	MeshRuntimeDataLayout* mesh_runtime_data_layout = nullptr;
	MeshRuntimeAllocation* mesh_runtime_allocation  = nullptr;
	
	TextureSourceData* texture_source_data = nullptr;
	TextureRuntimeDataLayout* texture_runtime_data_layout = nullptr;
	TextureDescriptorAllocation* texture_descriptor_allocation = nullptr;
	TextureRuntimeCpuStreamingRequest* texture_cpu_streaming_requests = nullptr;
	
	MaterialTextureData* material_texture_data = nullptr;
	
	WorldSourceData* world_source_data = nullptr;
	
	EditorIconCacheSettingsComponent* editor_icon_cache_settings = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct AssetCpuStreamingRequestQuery {
	MeshRuntimeCpuStreamingRequest*    mesh_cpu_streaming_requests    = nullptr;
	TextureRuntimeCpuStreamingRequest* texture_cpu_streaming_requests = nullptr;
};

void UpdateEditorAssetComponents(StackAllocator* alloc, AssetEntitySystem& asset_system);
