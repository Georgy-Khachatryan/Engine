#include "EditorEntities.h"
#include "Basic/BasicSaveLoad.h"

static void SaveLoad(SaveLoadBuffer& buffer, HashTableElement<u64, void>& element, u64 version) {
	SaveLoad(buffer, element.key);
}

void SaveLoad(SaveLoadBuffer& buffer, EditorSelectionStateComponent& data, u64 version) {
	SaveLoad(buffer, data.selected_entities_hash_table);
}

static void UpdateWorldAssetComponents(StackAllocator* alloc, AssetEntitySystem& asset_system) {
	ProfilerScope("UpdateWorldAssetComponents");
	
	auto* entity_array = QueryEntityTypeArray<WorldAssetType>(asset_system);
	auto streams = ExtractComponentStreams<WorldAssetType>(entity_array);
	
	for (u64 i : BitArrayIt(entity_array->created_mask)) {
		auto& source_data = streams.source_data[i];
		
		if (source_data.world_entity.guid == 0) {
			source_data.world_entity.guid = GenerateRandomNumber64(asset_system.guid_random_seed);
		}
	}
}

void UpdateEditorAssetComponents(StackAllocator* alloc, AssetEntitySystem& asset_system) {
	ProfilerScope("UpdateEditorAssetComponents");
	
	UpdateWorldAssetComponents(alloc, asset_system);
}
