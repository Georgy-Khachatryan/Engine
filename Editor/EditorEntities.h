#pragma once
#include "Basic/Basic.h"
#include "Engine/Entities.h"

NOTES(Meta::CustomSaveLoad{})
struct EditorSelectionStateComponent {
	// TODO: Add support for hash table reflection and save/load.
	HashTable<u64, void> selected_entities_hash_table;
};

NOTES(Meta::EntityType{ 1 }, Meta::ComponentQuery{})
struct EditorSelectionStateEntity {
	ECS::Component<GuidComponent> guid;
	ECS::Component<EditorSelectionStateComponent> selection_state;
};


NOTES(Meta::ComponentQuery{})
struct EntityEditorQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
	
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	ScaleComponent*    scale    = nullptr;
	
	MeshAssetGUID*     mesh_asset     = nullptr;
	MaterialAssetGUID* material_asset = nullptr;
	
	CameraComponent* camera = nullptr;
	
	LightComponent* light = nullptr;
	
	ToneMappingSettings*  tone_mapping_settings  = nullptr;
	AntiAliasingSettings* anti_aliasing_settings = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct AssetEditorQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
	
	MaterialAssetGUID* material_asset = nullptr;
	
	MeshSourceData* mesh_source_data = nullptr;
	MeshRuntimeDataLayout* mesh_runtime_data_layout = nullptr;
	MeshRuntimeAllocation* mesh_runtime_allocation  = nullptr;
	
	TextureSourceData* texture_source_data = nullptr;
	TextureRuntimeDataLayout* texture_runtime_data_layout = nullptr;
	TextureDescriptorAllocation* texture_descriptor_allocation = nullptr;
	
	MaterialTextureData* material_texture_data = nullptr;
};
