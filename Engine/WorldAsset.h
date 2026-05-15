#pragma once
#include "Basic/Basic.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"

NOTES(Meta::SaveLoadOptions{ SaveLoadFlags::SaveLoadToDisk })
struct WorldSourceData {
	u64 world_entity_guid = 0;
};

NOTES(Meta::EntityType{ 32 }, Meta::ComponentQuery{})
struct WorldAssetType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<WorldSourceData> source_data;
};
