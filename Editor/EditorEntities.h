#pragma once
#include "Basic/Basic.h"
#include "Engine/Entities.h"

NOTES(Meta::ComponentQuery{})
struct EntityEditorQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
	
	PositionComponent* position = nullptr;
	RotationComponent* rotation = nullptr;
	ScaleComponent*    scale    = nullptr;
	
	MeshAssetGUID* mesh_asset = nullptr;
	
	CameraComponent* camera = nullptr;
};

NOTES(Meta::ComponentQuery{})
struct AssetEditorQuery {
	GuidComponent* guid = nullptr;
	NameComponent* name = nullptr;
	
	MeshSourceData* mesh_source_data = nullptr;
};
