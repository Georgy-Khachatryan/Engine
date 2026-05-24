#include "Basic/Basic.h"
#include "LevelEditor.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"

static void EntityCreationComboBox(const char* label, const char* hint, EntitySystemBase& entity_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity, ArrayView<const EntityTypeID> entity_type_ids) {
	if (ImGui::BeginCombo(label, hint, ImGuiComboFlags_WidthFitPreview) == false) return;
	
	for (auto entity_type_id : entity_type_ids) {
		auto name = entity_type_name_table[entity_type_id.index];
		
		ImGuiScopeID(entity_type_id.index);
		if (ImGui::Selectable(name.data, false)) {
			auto entity_id = CreateEntity(entity_system, entity_type_id);
			auto entity = ExtractComponentStreams<GuidNameQuery>(&entity_system.entity_type_arrays[entity_type_id.index], entity_id);
			entity.name->name = StringCopy(&entity_system.heap, name);
			
			BeginUndoRedoGroup(undo_redo_system);
			UndoRedoCreateEntity(undo_redo_system, entity_system, entity.guid->guid);
			
			auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
			BeginUndoRedoCommand("Select Created Entity"_sl, undo_redo_system, entity_system, selection_state_entity.guid->guid);
			HashTableClear(selected_entities_hash_table);
			HashTableAddOrFind(selected_entities_hash_table, &entity_system.heap, entity.guid->guid);
			EndUndoRedoCommand(undo_redo_system);
			
			EndUndoRedoGroup(undo_redo_system);
		}
	}
	
	ImGui::EndCombo();
}

struct EntityViewTableEntry {
	EntityID entity_id;
	EntityTypeID entity_type_id;
	s32 score = 0;
};

static void ApplyEntitySelectionRequests(ImGuiMultiSelectIO* ms_io, ArrayView<EntityViewTableEntry> entity_view_table_entries, EntitySystemBase& entity_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity) {
	BeginUndoRedoCommand("Select Entities"_sl, undo_redo_system, entity_system, selection_state_entity.guid->guid);
	
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	for (auto& request : ms_io->Requests) {
		if (request.Type == ImGuiSelectionRequestType_SetAll) {
			if (request.Selected) {
				for (auto [entity_id, entity_type_id, score] : entity_view_table_entries) {
					auto streams = ExtractComponentStreams<GuidQuery>(&entity_system.entity_type_arrays[entity_type_id.index], entity_id);
					HashTableAddOrFind(selected_entities_hash_table, &entity_system.heap, streams.guid->guid);
				}
			} else {
				HashTableClear(selected_entities_hash_table);
			}
		} else if (request.Type == ImGuiSelectionRequestType_SetRange) {
			for (s64 index = request.RangeFirstItem; index <= request.RangeLastItem; index += 1) {
				auto [entity_id, entity_type_id, score] = entity_view_table_entries[index];
				auto streams = ExtractComponentStreams<GuidQuery>(&entity_system.entity_type_arrays[entity_type_id.index], entity_id);
				if (request.Selected) {
					HashTableAddOrFind(selected_entities_hash_table, &entity_system.heap, streams.guid->guid);
				} else {
					HashTableRemove(selected_entities_hash_table, streams.guid->guid);
				}
			}
		}
	}
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	EndUndoRedoCommand(undo_redo_system, is_dragging);
}

static ArrayView<EntityViewTableEntry> EntityQueryToArrayView(ArrayView<EntityTypeArray*> entity_view, StackAllocator* alloc, String search_pattern) {
	u32 entity_count = 0;
	for (auto* entity_array : entity_view) {
		entity_count += entity_array->count;
	}
	
	Array<EntityViewTableEntry> entity_view_table_entries;
	ArrayReserve(entity_view_table_entries, alloc, entity_count);
	
	for (auto* entity_array : entity_view) {
		auto streams = ExtractComponentStreams<GuidNameQuery>(entity_array);
		
		auto entity_type_id = entity_array->entity_type_id;
		auto entity_type_name = entity_type_name_table[entity_type_id.index];
		
		s32 type_name_score = search_pattern.count == 0 ? 0 : StringFuzzyMatch(search_pattern.data, entity_type_name.data);
		for (u64 i : BitArrayIt(entity_array->alive_mask)) {
			auto [name] = streams.name[i];
			
			s32 score = search_pattern.count == 0 || name.count == 0 ? type_name_score : Math::Max(StringFuzzyMatch(search_pattern.data, name.data), (type_name_score - 1) / 2);
			if (score >= 0) {
				auto& entry = ArrayEmplace(entity_view_table_entries);
				entry.entity_id      = EntityID{ (u32)i };
				entry.entity_type_id = entity_type_id;
				entry.score          = score;
			}
		}
	}
	
	if (search_pattern.count != 0) {
		HeapSort<EntityViewTableEntry>(entity_view_table_entries, [](const EntityViewTableEntry& lh, const EntityViewTableEntry& rh)-> bool {
			return lh.score > rh.score;
		});
	}
	
	return entity_view_table_entries;
}

static void EntityViewTable(StackAllocator* alloc, EntitySystemBase& entity_system, UndoRedoSystem& undo_redo_system, LevelEditorIO& level_editor_io, EditorSelectionStateEntity selection_state_entity, String search_pattern) {
	ProfilerScope("EntityViewTable");
	TempAllocationScope(alloc);
	
	auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInner | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("EntityViewTable", 3, flags) == false) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableHeadersRow();
	
	
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	auto entity_view_table_entries = EntityQueryToArrayView(QueryEntities<GuidNameQuery>(alloc, entity_system), alloc, search_pattern);
	
	auto* ms_io = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnClickVoid | ImGuiMultiSelectFlags_BoxSelect1d, (s32)selected_entities_hash_table.count, (s32)entity_view_table_entries.count);
	ApplyEntitySelectionRequests(ms_io, entity_view_table_entries, entity_system, undo_redo_system, selection_state_entity);
	
	
	ImGuiListClipper clipper;
	clipper.Begin((s32)entity_view_table_entries.count);
	if (ms_io->RangeSrcItem != -1) clipper.IncludeItemByIndex((s32)ms_io->RangeSrcItem);
	
	ImGui::PushStyleColor(ImGuiCol_NavCursor, 0u);
	while (clipper.Step()) {
		for (s32 index = clipper.DisplayStart; index < clipper.DisplayEnd; index += 1) {
			auto [entity_id, entity_type_id, score] = entity_view_table_entries[index];
			auto entity_type_name = entity_type_name_table[entity_type_id.index];
			
			auto streams = ExtractComponentStreams<GuidNameQuery>(&entity_system.entity_type_arrays[entity_type_id.index], entity_id);
			auto& [guid] = *streams.guid;
			auto& [name] = *streams.name;
			
			ImGui::TableNextRow();
			ImGuiScopeID((void*)guid);
			
			if (ImGui::TableSetColumnIndex(0)) {
				ImGui::Bullet();
				ImGui::SameLine();
				
				bool is_selected = HashTableFind(selected_entities_hash_table, guid) != nullptr;
				ImGui::SetNextItemSelectionUserData(index);
				ImGui::Selectable(name.count ? name.data : entity_type_name.data, is_selected, ImGuiSelectableFlags_SpanAllColumns);
				ImGui::EntityDragDropSource(entity_type_id, guid);
				
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					if (entity_type_id.index == ECS::GetEntityTypeID<CameraEntityType>::id.index) {
						level_editor_io.camera_entity_guid_to_set = guid;
					}
					
					if (entity_type_id.index == ECS::GetEntityTypeID<WorldAssetType>::id.index) {
						level_editor_io.world_asset_guid_to_load = guid;
					}
				}
			}
			
			if (ImGui::TableSetColumnIndex(1)) {
				ImGui::Text("0x%016llX", guid);
			}
			
			if (ImGui::TableSetColumnIndex(2)) {
				ImGui::TextUnformatted(entity_type_name.data);
			}
		}
	}
	ImGui::PopStyleColor();
	
	ms_io = ImGui::EndMultiSelect();
	ApplyEntitySelectionRequests(ms_io, entity_view_table_entries, entity_system, undo_redo_system, selection_state_entity);
}

static void EntityViewTableWithCreationAndSearch(StackAllocator* alloc, const char* creation_combo_box_label, UndoRedoSystem& undo_redo_system, EntitySystemBase& entity_system, EditorSelectionStateEntity selection_state_entity, ArrayView<const EntityTypeID> entity_type_ids, LevelEditorIO& level_editor_io) {
	EntityCreationComboBox("##CreateEntity", creation_combo_box_label, entity_system, undo_redo_system, selection_state_entity, entity_type_ids);
	
	ImGui::SameLine();
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
		ImGui::SetKeyboardFocusHere();
	}
	
	ImGui::SetNextItemWidth(-FLT_MIN);
	
	auto& search_pattern = selection_state_entity.selection_state->search_pattern;
	ImGui::InputTextWithHint("##SearchEntities", "Search", search_pattern, &entity_system.heap);
	
	EntityViewTable(alloc, entity_system, undo_redo_system, level_editor_io, selection_state_entity, search_pattern);
}

void EditorOutlinerWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity, LevelEditorIO& level_editor_io) {
	static const EntityTypeID creatable_world_entity_type_ids[] = {
		ECS::GetEntityTypeID<MeshEntityType>::id,
		ECS::GetEntityTypeID<LightEntityType>::id,
		ECS::GetEntityTypeID<CameraEntityType>::id,
	};
	
	ImGui::Begin("Outliner");
	EntityViewTableWithCreationAndSearch(alloc, "Create Entity", undo_redo_system, world_system, selection_state_entity, ArrayViewCreate(creatable_world_entity_type_ids), level_editor_io);
	ImGui::End();
}

void EditorAssetBrowserWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity selection_state_entity, LevelEditorIO& level_editor_io) {
	static const EntityTypeID creatable_asset_entity_type_ids[] = {
		ECS::GetEntityTypeID<MeshAssetType>::id,
		ECS::GetEntityTypeID<TextureAssetType>::id,
		ECS::GetEntityTypeID<MaterialAssetType>::id,
		ECS::GetEntityTypeID<WorldAssetType>::id,
	};
	
	ImGui::Begin("Asset Browser");
	EntityViewTableWithCreationAndSearch(alloc, "Create Asset", undo_redo_system, asset_system, selection_state_entity, ArrayViewCreate(creatable_asset_entity_type_ids), level_editor_io);
	ImGui::End();
}
