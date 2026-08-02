#pragma once
#include "Basic/Basic.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"

using WorldEntityGUID = EntityGUID<struct WorldEntityType>;

NOTES(Meta::SaveLoadOptions{ SaveLoadFlags::SaveLoadToDisk })
struct WorldSourceData {
	WorldEntityGUID world_entity;
};

NOTES(Meta::EntityType{ 32 }, Meta::ComponentQuery{})
struct WorldAssetType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<WorldSourceData> source_data;
};
