#include "Basic/Basic.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"
#include "LevelEditor.h"

compile_const auto assets_save_load_path = "./Assets/Assets.csb"_sl;

static void CreateDefaultAssetSystem(AssetEntitySystem& asset_system) {
	CreateEntity<EditorSelectionStateEntity>(asset_system);
	
	auto world_asset = CreateEntity<WorldAssetType>(asset_system);
	world_asset.name->name = StringCopy(&asset_system.heap, "DefaultWorld"_sl);
	world_asset.source_data->world_entity_guid = GenerateRandomNumber64(asset_system.guid_random_seed);
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

u64 LoadOrCreateDefaultEntitySystems(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system) {
	TempAllocationScope(alloc);
	
	if (SaveLoadEntitySystemToFile(alloc, asset_system, assets_save_load_path, SaveLoadDirection::Loading) == false) {
		CreateDefaultAssetSystem(asset_system);
	}
	
	auto world_asset = QueryFirstEntityByType<WorldAssetType>(asset_system);
	u64 world_entity_guid = world_asset.source_data->world_entity_guid;
	
	auto entities_save_load_path = StringFormat(alloc, "./Assets/%x..csb"_sl, world_entity_guid);
	if (SaveLoadEntitySystemToFile(alloc, world_system, entities_save_load_path, SaveLoadDirection::Loading) == false) {
		CreateDefaultWorldSystem(world_system, world_entity_guid);
	}
	
	return world_entity_guid;
}


static void DuplicateSelectedEntities(StackAllocator* alloc, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity) {
	TempAllocationScope(alloc);
	
	SaveLoadBuffer buffer;
	buffer.alloc = alloc;
	buffer.heap  = &world_system.heap;
	buffer.direction = SaveLoadDirection::Saving;
	
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	for (auto [guid] : selected_entities_hash_table) {
		auto typed_entity_id = FindEntityByGUID(world_system, guid);
		auto* entity_array = &world_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
		SaveLoadEntityForTooling(buffer, entity_array, typed_entity_id.entity_id);
	}
	
	buffer.data.count = 0;
	buffer.direction  = SaveLoadDirection::Loading;
	
	Array<u64> new_entity_guids;
	ArrayReserve(new_entity_guids, alloc, selected_entities_hash_table.count);
	
	BeginUndoRedoGroup(undo_redo_system);
	for (auto [guid] : selected_entities_hash_table) {
		auto src_typed_entity_id = FindEntityByGUID(world_system, guid);
		auto* entity_array = &world_system.entity_type_arrays[src_typed_entity_id.entity_type_id.index];
		
		auto entity_id = CreateEntity(world_system, src_typed_entity_id.entity_type_id);
		SaveLoadEntityForTooling(buffer, entity_array, entity_id);
		
		auto guid_query = ExtractComponentStreams<GuidQuery>(entity_array, entity_id);
		ArrayAppend(new_entity_guids, guid_query.guid->guid);
		
		UndoRedoCreateEntity(undo_redo_system, world_system, guid_query.guid->guid);
	}
	
	
	BeginUndoRedoCommand("Select Duplicated Entities"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	HashTableClear(selected_entities_hash_table);
	for (u64 guid : new_entity_guids) {
		HashTableAddOrFind(selected_entities_hash_table, guid);
	}
	EndUndoRedoCommand(undo_redo_system);
	
	EndUndoRedoGroup(undo_redo_system);
}

static void RemoveSelectedEntities(WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity, u64 camera_entity_guid) {
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	
	BeginUndoRedoGroup(undo_redo_system);
	HashTableRemove(selected_entities_hash_table, camera_entity_guid); // Don't remove the active camera.
	
	for (auto& [guid] : selected_entities_hash_table) {
		UndoRedoRemoveEntity(undo_redo_system, world_system, guid);
		RemoveEntityByGUID(world_system, guid);
	}
	
	BeginUndoRedoCommand("Deselect Removed Entities"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	HashTableClear(selected_entities_hash_table);
	EndUndoRedoCommand(undo_redo_system);
	
	EndUndoRedoGroup(undo_redo_system);
}


static void LevelEditorSaveLoadShortcuts(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity asset_selection_state_entity, u64& world_entity_guid) {
	
	bool should_save_scene = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
	bool should_load_scene = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
	
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
		if (selected_asset_entities_hash_table.count == 1) {
			u64 selected_asset_guid = (*selected_asset_entities_hash_table.begin()).key;
			auto selected_world_asset = QueryEntityByGUID<WorldAssetType>(asset_system, selected_asset_guid);
			if (selected_world_asset.source_data.data) {
				world_entity_guid_to_load = selected_world_asset.source_data->world_entity_guid;
			}
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
		auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, world_entity_guid);
		RemoveSelectedEntities(world_system, undo_redo_system, world_selection_state_entity, world_entity.camera_entity->guid);
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


void LevelEditorUpdate(StackAllocator* alloc, GraphicsContext* graphics_context, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, u64& world_entity_guid) {
	ProfilerScope("LevelEditorUpdate");
	
	auto asset_selection_state_entity = QueryFirstEntityByType<EditorSelectionStateEntity>(asset_system);
	LevelEditorSaveLoadShortcuts(alloc, undo_redo_system, world_system, asset_system, asset_selection_state_entity, world_entity_guid);
	
	auto world_selection_state_entity = QueryFirstEntityByType<EditorSelectionStateEntity>(world_system);
	LevelEditorShortcuts(alloc, undo_redo_system, world_system, asset_system, world_selection_state_entity, asset_selection_state_entity, world_entity_guid);
	
	EditorUndoRedoHistoryWindow(undo_redo_system);
	
	EditorShaderStatisticsWindow(alloc, graphics_context);
	
	EditorOutlinerWindow(alloc, undo_redo_system, world_system, world_selection_state_entity, world_entity_guid);
	
	EditorAssetBrowserWindow(alloc, undo_redo_system, asset_system, asset_selection_state_entity);
	
	EditorPropertiesWindow(alloc, undo_redo_system, world_system, asset_system, world_selection_state_entity, asset_selection_state_entity, world_entity_guid);
	
	EditorViewportWindow(alloc, undo_redo_system, world_system, asset_system, world_selection_state_entity, world_entity_guid, graphics_context);
}
