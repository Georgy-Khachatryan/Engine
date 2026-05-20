#include "EditorEntities.h"
#include "Basic/BasicSaveLoad.h"

static void SaveLoad(SaveLoadBuffer& buffer, HashTableElement<u64, void>& element, u64 version) {
	SaveLoad(buffer, element.key);
}

void SaveLoad(SaveLoadBuffer& buffer, EditorSelectionStateComponent& data, u64 version) {
	SaveLoad(buffer, data.selected_entities_hash_table);
}


void UpdateEditorEntityComponents(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system) {
	ProfilerScope("UpdateEditorEntityComponents");
	
	for (auto* entity_array : QueryEntities<WorldAssetType>(alloc, asset_system)) {
		ProfilerScope("WorldAssetComponentUpdate");
		auto streams = ExtractComponentStreams<WorldAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->created_mask)) {
			auto& source_data = streams.source_data[i];
			
			if (source_data.world_entity_guid == 0) {
				source_data.world_entity_guid = GenerateRandomNumber64(asset_system.guid_random_seed);
			}
		}
	}
}
