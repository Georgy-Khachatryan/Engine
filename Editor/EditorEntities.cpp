#include "EditorEntities.h"
#include "Basic/BasicSaveLoad.h"

static void SaveLoad(SaveLoadBuffer& buffer, HashTableElement<u64, void>& element, u64 version) {
	SaveLoad(buffer, element.key);
}

void SaveLoad(SaveLoadBuffer& buffer, EditorSelectionStateComponent& data, u64 version) {
	SaveLoad(buffer, data.selected_entities_hash_table);
}
