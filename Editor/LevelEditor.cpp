#include "Basic/Basic.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"
#include "LevelEditor.h"
#include "Renderer/Renderer.h"

compile_const auto assets_save_load_path = "./Assets/Assets.csb"_sl;

static void CreateDefaultAssetSystem(AssetEntitySystem& asset_system) {
	CreateEntity<EditorSelectionStateEntity>(asset_system);
	CreateEntity<EditorSettingsEntityType>(asset_system);
	
	auto world_asset = CreateEntity<WorldAssetType>(asset_system);
	world_asset.name->name = StringCopy(&asset_system.heap, "DefaultWorld"_sl);
	world_asset.source_data->world_entity.guid = GenerateRandomNumber64(asset_system.guid_random_seed);
}

static void CreateDefaultWorldSystem(WorldEntitySystem& world_system, u64 world_entity_guid) {
	CreateEntity<EditorSelectionStateEntity>(world_system);
	
	auto world_entity  = CreateEntity<WorldEntityType>(world_system, world_entity_guid);
	auto camera_entity = CreateEntity<CameraEntityType>(world_system);
	auto mesh_entity   = CreateEntity<MeshEntityType>(world_system);
	auto global_light_entity = CreateEntity<LightEntityType>(world_system);
	
	camera_entity.rotation->rotation =
		Math::AxisAngleToQuat(float3(0.f, 0.f, 1.f), -90.f * Math::degrees_to_radians) *
		Math::AxisAngleToQuat(float3(1.f, 0.f, 0.f), -90.f * Math::degrees_to_radians);
	
	camera_entity.name->name       = StringCopy(&world_system.heap, "DefaultCamera"_sl);
	global_light_entity.name->name = StringCopy(&world_system.heap, "DefaultGlobalLight"_sl);
	global_light_entity.light->type = LightType::Global;
	
	world_entity.camera_entity->guid       = camera_entity.guid->guid;
	world_entity.global_light_entity->guid = global_light_entity.guid->guid;
}

static u64 LoadOrCreateDefaultEntitySystems(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system) {
	TempAllocationScope(alloc);
	
	if (SaveLoadEntitySystemToFile(alloc, asset_system, assets_save_load_path, SaveLoadDirection::Loading) == false) {
		CreateDefaultAssetSystem(asset_system);
	}
	
	auto world_asset = QueryFirstEntityByType<WorldAssetType>(asset_system);
	u64 world_entity_guid = world_asset.source_data->world_entity.guid;
	
	auto entities_save_load_path = StringFormat(alloc, "./Assets/%x..csb"_sl, world_entity_guid);
	if (SaveLoadEntitySystemToFile(alloc, world_system, entities_save_load_path, SaveLoadDirection::Loading) == false) {
		CreateDefaultWorldSystem(world_system, world_entity_guid);
	}
	
	return world_entity_guid;
}

struct LevelEditor {
	WorldEntitySystem world_system;
	VirtualResourceTable* resource_table = nullptr;
	
	u64 world_entity_guid = 0;
};

LevelEditor* CreateLevelEditor(StackAllocator* alloc, GraphicsContext* graphics_context, AssetEntitySystem& asset_system) {
	auto* level_editor = NewFromAlloc(alloc, LevelEditor);
	level_editor->resource_table = CreateResourceTable(alloc);
	InitializeEntitySystem(level_editor->world_system, alloc);
	
	level_editor->world_entity_guid = LoadOrCreateDefaultEntitySystems(alloc, level_editor->world_system, asset_system);
	
	return level_editor;
}

void ReleaseLevelEditor(LevelEditor* level_editor, GraphicsContext* graphics_context) {
	ReleaseEntitySystemGpuStreamAllocations(graphics_context, level_editor->world_system);
	ReleaseHeapAllocator(level_editor->world_system.heap);
	ReleaseResourceTable(graphics_context, level_editor->resource_table);
}


static void DeselectChildrenOfSelectedEntities(WorldEntitySystem& world_system, HashTable<u64, void>& selected_entities_hash_table) {
	for (auto& [guid] : selected_entities_hash_table) {
		auto entity = QueryEntityByGUID<GuidHierarchyQuery>(world_system, guid);
		while (entity.hierarchy != nullptr && entity.hierarchy->parent.guid != 0) {
			if (HashTableFind(selected_entities_hash_table, entity.hierarchy->parent.guid) != nullptr) {
				HashTableRemove(selected_entities_hash_table, guid);
				break;
			} else {
				entity = QueryEntityByGUID<GuidHierarchyQuery>(world_system, entity.hierarchy->parent.guid);
			}
		}
	}
}

static void SaveLoadEntityAndChildrenForTooling(SaveLoadBuffer& buffer, WorldEntitySystem& world_system, u64 guid) {
	auto typed_entity_id = FindEntityByGUID(world_system, guid);
	auto* entity_array = QueryEntityTypeArray(world_system, typed_entity_id.entity_type_id);
	SaveLoadEntityForTooling(buffer, entity_array, typed_entity_id.entity_id);
	
	auto entity = ExtractComponentStreams<GuidHierarchyQuery>(entity_array, typed_entity_id.entity_id);
	if (entity.hierarchy != nullptr) {
		for (auto [child_guid] : entity.hierarchy->children) {
			SaveLoadEntityAndChildrenForTooling(buffer, world_system, child_guid);
		}
	}
}

static u64 DuplicateEntityAndChildren(SaveLoadBuffer& buffer, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, u64 guid, Array<u64>& new_entity_guids, u64 new_parent_guid) {
	auto src_typed_entity_id = FindEntityByGUID(world_system, guid);
	auto* entity_array = QueryEntityTypeArray(world_system, src_typed_entity_id.entity_type_id);
	
	auto entity_id = CreateEntity(world_system, src_typed_entity_id.entity_type_id);
	SaveLoadEntityForTooling(buffer, entity_array, entity_id);
	
	auto entity = ExtractComponentStreams<GuidHierarchyQuery>(entity_array, entity_id);
	u64 new_guid = entity.guid->guid;
	
	ArrayAppend(new_entity_guids, buffer.alloc, new_guid);
	
	if (entity.hierarchy != nullptr) {
		auto& hierarchy = *entity.hierarchy;
		
		if (new_parent_guid != 0) { // The new parent will handle adding the child to it's array.
			hierarchy.parent.guid = new_parent_guid;
		} else if (hierarchy.parent.guid != 0) { // Append the new entity to an existing parent.
			auto parent = QueryEntityByGUID<GuidHierarchyQuery>(world_system, hierarchy.parent.guid);
			u64 index = ArrayFind<GuidComponent>(parent.hierarchy->children, GuidComponent{ guid });
			DebugAssert(index != u64_max, "Parent doesn't have it's child in the children array.");
			
			BeginUndoRedoCommand("Create Child"_sl, undo_redo_system, world_system, hierarchy.parent.guid);
			ArrayInsert(parent.hierarchy->children, &world_system.heap, index, *entity.guid);
			EndUndoRedoCommand(undo_redo_system);
		}
		
		for (auto& [child_guid] : hierarchy.children) {
			child_guid = DuplicateEntityAndChildren(buffer, world_system, undo_redo_system, child_guid, new_entity_guids, new_guid);
		}
	}
	
	UndoRedoCreateEntity(undo_redo_system, world_system, new_guid);
	
	return new_guid;
}

static void DuplicateSelectedEntities(StackAllocator* alloc, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity) {
	TempAllocationScope(alloc);
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	
	SaveLoadBuffer buffer;
	buffer.alloc = alloc;
	buffer.heap  = &world_system.heap;
	buffer.direction = SaveLoadDirection::Saving;
	
	BeginUndoRedoGroup(undo_redo_system);
	
	// Don't directly duplicate entities whose parents are duplicated. Parents will depth first duplicate their children.
	BeginUndoRedoCommand("Deselect Entities Before Duplicating"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	DeselectChildrenOfSelectedEntities(world_system, selected_entities_hash_table);
	EndUndoRedoCommand(undo_redo_system);
	
	for (auto [guid] : selected_entities_hash_table) {
		SaveLoadEntityAndChildrenForTooling(buffer, world_system, guid);
	}
	
	buffer.data.count = 0;
	buffer.direction  = SaveLoadDirection::Loading;
	
	Array<u64> new_entity_guids;
	ArrayReserve(new_entity_guids, alloc, selected_entities_hash_table.count);
	
	for (auto [guid] : selected_entities_hash_table) {
		DuplicateEntityAndChildren(buffer, world_system, undo_redo_system, guid, new_entity_guids, 0);
	}
	
	BeginUndoRedoCommand("Select Duplicated Entities"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	HashTableClear(selected_entities_hash_table);
	for (u64 guid : new_entity_guids) {
		HashTableAddOrFind(selected_entities_hash_table, guid);
	}
	EndUndoRedoCommand(undo_redo_system);
	
	EndUndoRedoGroup(undo_redo_system);
}

static void RemoveEntityAndChildren(WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, u64 guid, bool is_parent_getting_removed) {
	auto entity = QueryEntityByGUID<GuidHierarchyQuery>(world_system, guid);
	if (entity.hierarchy != nullptr) {
		auto& hierarchy = *entity.hierarchy;
		
		for (auto [child_guid] : hierarchy.children) {
			RemoveEntityAndChildren(world_system, undo_redo_system, child_guid, true);
		}
		
		// Don't patch child array of the parent if it's also getting removed. It's not necessary, and we're actually iterating over it right now.
		if (is_parent_getting_removed == false && hierarchy.parent.guid != 0) {
			auto parent = QueryEntityByGUID<GuidHierarchyQuery>(world_system, hierarchy.parent.guid);
			u64 index = ArrayFind<GuidComponent>(parent.hierarchy->children, GuidComponent{ guid });
			DebugAssert(index != u64_max, "Parent doesn't have it's child in the children array.");
			
			BeginUndoRedoCommand("Remove Child"_sl, undo_redo_system, world_system, hierarchy.parent.guid);
			ArrayErase(parent.hierarchy->children, index);
			EndUndoRedoCommand(undo_redo_system);
		}
	}
	
	UndoRedoRemoveEntity(undo_redo_system, world_system, guid);
	RemoveEntityByGUID(world_system, guid);
}

static void RemoveSelectedEntities(WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity, u64 world_entity_guid) {
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	
	BeginUndoRedoGroup(undo_redo_system);
	BeginUndoRedoCommand("Deselect Entities Before Removing"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	
	// Don't remove entities referenced by the world. Would be nice to have a more generic way to express this (maybe via hierarchy?).
	auto world_entity = QueryEntityByGUID<WorldEntityReferenceComponentsQuery>(world_system, world_entity_guid);
	HashTableRemove(selected_entities_hash_table, world_entity.camera_entity->guid);
	HashTableRemove(selected_entities_hash_table, world_entity.global_light_entity->guid);
	
	// Don't directly remove entities whose parents are removed. Parents will depth first remove their children.
	DeselectChildrenOfSelectedEntities(world_system, selected_entities_hash_table);
	EndUndoRedoCommand(undo_redo_system);
	
	for (auto& [guid] : selected_entities_hash_table) {
		RemoveEntityAndChildren(world_system, undo_redo_system, guid, false);
	}
	
	BeginUndoRedoCommand("Deselect Removed Entities"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	HashTableClear(selected_entities_hash_table);
	EndUndoRedoCommand(undo_redo_system);
	
	EndUndoRedoGroup(undo_redo_system);
}


static void LevelEditorSaveLoadShortcuts(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity asset_selection_state_entity, LevelEditorIO& level_editor_io, u64& world_entity_guid) {
	
	bool should_save_scene = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
	bool should_load_scene = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused) || (level_editor_io.world_asset_guid_to_load != 0);
	
	// Save the current scene before loading another one.
	if (should_save_scene || should_load_scene) {
		TempAllocationScope(alloc);
		auto entities_save_load_path = StringFormat(alloc, "./Assets/%x..csb"_sl, world_entity_guid);
		SaveLoadEntitySystemToFile(alloc, world_system, entities_save_load_path, SaveLoadDirection::Saving);
		SaveLoadEntitySystemToFile(alloc, asset_system, assets_save_load_path,   SaveLoadDirection::Saving);
	}
	
	if (should_load_scene) {
		TempAllocationScope(alloc);
		u64 world_entity_guid_to_load = 0;
		
		auto& selected_asset_entities_hash_table = asset_selection_state_entity.selection_state->selected_entities_hash_table;
		if (level_editor_io.world_asset_guid_to_load != 0 || selected_asset_entities_hash_table.count == 1) {
			u64 selected_asset_guid = level_editor_io.world_asset_guid_to_load != 0 ? level_editor_io.world_asset_guid_to_load : (*selected_asset_entities_hash_table.begin()).key;
			
			auto selected_world_asset = QueryEntityByGUID<WorldAssetType>(asset_system, selected_asset_guid);
			if (selected_world_asset.source_data.data) {
				world_entity_guid_to_load = selected_world_asset.source_data->world_entity.guid;
			}
			
			level_editor_io.world_asset_guid_to_load = 0;
		}
		
		if (world_entity_guid_to_load != 0) {
			auto entities_save_load_path = StringFormat(alloc, "./Assets/%x..csb"_sl, world_entity_guid_to_load);
			if (SaveLoadEntitySystemToFile(alloc, world_system, entities_save_load_path, SaveLoadDirection::Loading) == false) {
				ResetEntitySystem(world_system);
				CreateDefaultWorldSystem(world_system, world_entity_guid_to_load);
			}
			ResetUndoRedoSystem(undo_redo_system);
			world_entity_guid = world_entity_guid_to_load;
		}
	}
}

static void LevelEditorShortcuts(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, EditorSelectionStateEntity asset_selection_state_entity, u64 world_entity_guid) {
	
	if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
		RemoveSelectedEntities(world_system, undo_redo_system, world_selection_state_entity, world_entity_guid);
	}
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, ImGuiInputFlags_RouteGlobal)) {
		DuplicateSelectedEntities(alloc, world_system, undo_redo_system, world_selection_state_entity);
	}
	
	if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal)) {
		BeginUndoRedoGroup(undo_redo_system);
		
		BeginUndoRedoCommand("Deselect Entities"_sl, undo_redo_system, world_system, world_selection_state_entity.guid->guid);
		HashTableClear(world_selection_state_entity.selection_state->selected_entities_hash_table);
		EndUndoRedoCommand(undo_redo_system);
		
		BeginUndoRedoCommand("Deselect Assets"_sl, undo_redo_system, asset_system, asset_selection_state_entity.guid->guid);
		HashTableClear(asset_selection_state_entity.selection_state->selected_entities_hash_table);
		EndUndoRedoCommand(undo_redo_system);
		
		EndUndoRedoGroup(undo_redo_system);
	}
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat)) {
		ExecuteUndo(undo_redo_system);
	}
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat)) {
		ExecuteRedo(undo_redo_system);
	}
}

static void ProcessLevelEditorIO(UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, LevelEditorIO& level_editor_io, u64 world_entity_guid) {
	if (level_editor_io.camera_entity_guid_to_set != 0) {
		BeginUndoRedoCommand("Select Active Camera"_sl, undo_redo_system, world_system, world_entity_guid);
		auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, world_entity_guid);
		world_entity.camera_entity->guid = level_editor_io.camera_entity_guid_to_set;
		EndUndoRedoCommand(undo_redo_system);
		
		level_editor_io.camera_entity_guid_to_set = 0;
	}
}


void LevelEditorUpdate(StackAllocator* alloc, GraphicsContext* graphics_context, UndoRedoSystem& undo_redo_system, AssetEntitySystem& asset_system, LevelEditorIO& level_editor_io, Array<EditorWorldView>& editor_world_views) {
	ProfilerScope("LevelEditorUpdate");
	
	auto& world_system = level_editor_io.level_editor->world_system;
	auto& world_entity_guid = level_editor_io.level_editor->world_entity_guid;
	
	auto asset_selection_state_entity = QueryFirstEntityByType<EditorSelectionStateEntity>(asset_system);
	LevelEditorSaveLoadShortcuts(alloc, undo_redo_system, world_system, asset_system, asset_selection_state_entity, level_editor_io, world_entity_guid);
	
	auto world_selection_state_entity = QueryFirstEntityByType<EditorSelectionStateEntity>(world_system);
	LevelEditorShortcuts(alloc, undo_redo_system, world_system, asset_system, world_selection_state_entity, asset_selection_state_entity, world_entity_guid);
	
	EditorUndoRedoHistoryWindow(undo_redo_system);
	
	EditorShaderStatisticsWindow(alloc, graphics_context);
	EditorResourceStatisticsWindow(alloc, level_editor_io.level_editor->resource_table);
	
	EditorOutlinerWindow(alloc, undo_redo_system, world_system, world_selection_state_entity, level_editor_io);
	
	EditorAssetBrowserWindow(alloc, undo_redo_system, asset_system, asset_selection_state_entity, level_editor_io);
	
	ProcessLevelEditorIO(undo_redo_system, world_system, level_editor_io, world_entity_guid);
	
	EditorPropertiesWindow(alloc, undo_redo_system, world_system, asset_system, world_selection_state_entity, asset_selection_state_entity, world_entity_guid);
	
	EditorViewportWindow(alloc, undo_redo_system, world_system, asset_system, world_selection_state_entity, world_entity_guid, graphics_context, level_editor_io.level_editor->resource_table, editor_world_views);
	
	TerrainEditorWindow(alloc, undo_redo_system, world_system, world_selection_state_entity);
	
	EditorIconCacheUpdate(alloc, level_editor_io.icon_cache, asset_system, editor_world_views);
}
