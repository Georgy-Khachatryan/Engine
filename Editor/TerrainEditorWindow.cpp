#include "Basic/Basic.h"
#include "LevelEditor.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"
#include "Renderer/TerrainEditorEntities.h"


static void TerrainLayerCreationComboBox(UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity, TerrainEditorLayerStackEntityType layer_stack_entity) {
	static const EntityTypeID creatable_entity_type_ids[] = {
		ECS::GetEntityTypeID<TerrainHeightLayerNoiseEntityType>::id,
		ECS::GetEntityTypeID<TerrainHeightLayerDistortionEntityType>::id,
		ECS::GetEntityTypeID<TerrainHeightLayerStrataEntityType>::id,
		ECS::GetEntityTypeID<TerrainHeightLayerErosionEntityType>::id,
	};
	
	auto& style = ImGui::GetStyle();
	float combo_box_width = ImGui::CalcTextSize("Create Layer").x + ImGui::GetFrameHeight() + style.FramePadding.x * 2.f;
	
	ImGui::SameLine(ImGui::GetContentRegionAvail().x + 12.f - combo_box_width); // TODO: Nicer UI for layer creation.
	
	BeginUndoRedoGroup(undo_redo_system);
	u64 new_terrain_layer_guid = EntityCreationComboBox("##CreateTerrainEntity", "Create Layer", world_system, undo_redo_system, selection_state_entity, ArrayViewCreate(creatable_entity_type_ids));
	if (new_terrain_layer_guid != 0) {
		BeginUndoRedoCommand("Create Child"_sl, undo_redo_system, world_system, layer_stack_entity.guid->guid);
		ArrayAppend(layer_stack_entity.hierarchy->children, &world_system.heap, GuidComponent{ new_terrain_layer_guid });
		EndUndoRedoCommand(undo_redo_system);
		
		auto new_terrain_layer = QueryEntityByGUID<TerrainEditorLayerQuery>(world_system, new_terrain_layer_guid);
		BeginUndoRedoCommand("Set Parent"_sl, undo_redo_system, world_system, new_terrain_layer_guid);
		new_terrain_layer.hierarchy->parent = *layer_stack_entity.guid;
		EndUndoRedoCommand(undo_redo_system);
	}
	EndUndoRedoGroup(undo_redo_system);
}

void TerrainEditorWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity) {
	ImGui::Begin("Terrain Editor");
	defer{ ImGui::End(); };
	
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(world_system);
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	
	bool is_open = ImGui::CollapsingHeader("Height Layers", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
	TerrainLayerCreationComboBox(undo_redo_system, world_system, selection_state_entity, layer_stack_entity);
	
	if (is_open) {
		u64  dropped_guid  = 0;
		u64  drop_to_index = 0;
		auto line_position = ImVec2(0.f, 0.f);
		bool is_delivery   = false;
		
		auto& children = layer_stack_entity.hierarchy->children;
		for (u64 index = 0; index < children.count; index += 1) {
			auto [layer_entity_guid] = children[index];
			ImGuiScopeID((void*)layer_entity_guid);
			
			auto terrain_layer = QueryEntityByGUID<TerrainEditorLayerQuery>(world_system, layer_entity_guid);
			bool is_selected = HashTableFind(selected_entities_hash_table, layer_entity_guid) != nullptr;
			
			auto entity_type_id = terrain_layer.array->entity_type_id;
			auto entity_type_name = entity_type_name_table[entity_type_id.index];
			
			auto cursor_position_before = ImGui::GetCursorScreenPos();
			
			ImGui::Bullet();
			ImGui::SameLine();
			
			auto name = terrain_layer.name->name;
			if (ImGui::Selectable(name.data ? name.data : entity_type_name.data, is_selected)) {
				BeginUndoRedoCommand("Select Entity"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
				HashTableClear(selected_entities_hash_table);
				HashTableAddOrFind(selected_entities_hash_table, &world_system.heap, layer_entity_guid);
				EndUndoRedoCommand(undo_redo_system);
			}
			
			auto cursor_position_after = ImGui::GetCursorScreenPos();
			
			
			compile_const char* payload_type_name = "TerrainLayer";
			if (ImGui::BeginDragDropSource()) {
				ImGui::TextUnformatted(name.data ? name.data : entity_type_name.data);
				ImGui::SetDragDropPayload(payload_type_name, &layer_entity_guid, sizeof(u64));
				ImGui::EndDragDropSource();
			}
			
			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload(payload_type_name, ImGuiDragDropFlags_AcceptPeekOnly)) {
					memcpy(&dropped_guid, payload->Data, sizeof(u64));
					
					bool is_mouse_cursor_below = ImGui::GetMousePos().y > (cursor_position_before.y + cursor_position_after.y) * 0.5f;
					drop_to_index = is_mouse_cursor_below ? index + 1 : index;
					line_position = is_mouse_cursor_below ? cursor_position_after : cursor_position_before;
					is_delivery   = payload->IsDelivery();
				}
				ImGui::EndDragDropTarget();
			}
		}
		
		u64 index = dropped_guid != 0 ? ArrayFind<GuidComponent>(children, GuidComponent{ dropped_guid }) : u64_max;
		if (dropped_guid != 0 && (drop_to_index < index || drop_to_index > index + 1)) {
			auto* draw_list = ImGui::GetWindowDrawList();
			auto p0 = line_position;
			auto p1 = line_position + ImVec2(ImGui::GetContentRegionAvail().x, 0.f);
			
			draw_list->AddLine(p0, p1, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 1.f);
			draw_list->AddCircleFilled(p0, 3.f, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
			draw_list->AddCircleFilled(p1, 3.f, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
		}
		
		if (is_delivery && dropped_guid != 0 && (drop_to_index < index || drop_to_index > index + 1)) {
			BeginUndoRedoCommand("Reorder Children"_sl, undo_redo_system, world_system, layer_stack_entity.guid->guid);
			ArrayErase(children, index);
			ArrayInsert(children, drop_to_index < index ? drop_to_index : (drop_to_index - 1), GuidComponent{ dropped_guid });
			EndUndoRedoCommand(undo_redo_system);
		}
	}
}
