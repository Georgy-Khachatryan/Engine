#include "Basic/Basic.h"
#include "LevelEditor.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"
#include "Renderer/TerrainEditorEntities.h"

#include <SDK/imgui/imgui_internal.h>

static void TerrainLayerCreationComboBox(UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity, TerrainEditorLayerQuery layer_stack_entity) {
	static const EntityTypeID creatable_entity_type_ids[] = {
		ECS::GetEntityTypeID<TerrainHeightLayerNoiseEntityType>::id,
		ECS::GetEntityTypeID<TerrainHeightLayerDistortionEntityType>::id,
		ECS::GetEntityTypeID<TerrainHeightLayerStrataEntityType>::id,
		ECS::GetEntityTypeID<TerrainHeightLayerErosionEntityType>::id,
	};
	
	auto& style = ImGui::GetStyle();
	float combo_box_width = ImGui::CalcTextSize("Create Layer").x + ImGui::GetFrameHeight() + style.FramePadding.x * 2.f;
	
	ImGui::SameLine(ImGui::GetWindowWidth() - style.WindowPadding.x - combo_box_width); // TODO: Nicer UI for layer creation.
	
	BeginUndoRedoGroup(undo_redo_system);
	u64 new_terrain_layer_guid = EntityCreationComboBox("##CreateTerrainEntity", "Create Layer", world_system, undo_redo_system, selection_state_entity, ArrayViewCreate(creatable_entity_type_ids));
	if (new_terrain_layer_guid != 0) {
		auto* storage = ImGui::GetStateStorage();
		storage->SetBool((ImGuiID)layer_stack_entity.guid->guid, true);
		
		BeginUndoRedoCommand("Create Child"_sl, undo_redo_system, world_system, layer_stack_entity.guid->guid);
		ArrayAppend(layer_stack_entity.hierarchy->children, &world_system.heap, GuidComponent{ new_terrain_layer_guid });
		EndUndoRedoCommand(undo_redo_system);
		
		auto new_terrain_layer = QueryEntityByGUID<HierarchyQuery>(world_system, new_terrain_layer_guid);
		BeginUndoRedoCommand("Set Parent"_sl, undo_redo_system, world_system, new_terrain_layer_guid);
		new_terrain_layer.hierarchy->parent = *layer_stack_entity.guid;
		EndUndoRedoCommand(undo_redo_system);
	}
	EndUndoRedoGroup(undo_redo_system);
}

struct TerrainEditorLayerEntry {
	u64 guid = 0;
	u32 parent_index = 0;
};

static void BuildVisibleTerrainLayerArray(StackAllocator* alloc, Array<TerrainEditorLayerEntry>& layers, WorldEntitySystem& world_system, u64 entity_guid, u32 parent_index = u32_max) {
	u32  index = (u32)layers.count;
	auto layer = QueryEntityByGUID<HierarchyQuery>(world_system, entity_guid);
	
	TerrainEditorLayerEntry layer_entry;
	layer_entry.guid         = entity_guid;
	layer_entry.parent_index = parent_index;
	ArrayAppend(layers, alloc, layer_entry);
	
	auto* storage = ImGui::GetStateStorage();
	if (storage->GetBool((ImGuiID)entity_guid, parent_index == u32_max)) {
		for (auto [child_guid] : layer.hierarchy->children) {
			BuildVisibleTerrainLayerArray(alloc, layers, world_system, child_guid, index);
		}
	}
}

static void ApplyEntitySelectionRequests(ImGuiMultiSelectIO* ms_io, Array<TerrainEditorLayerEntry>& layers, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity) {
	BeginUndoRedoCommand("Select Entities"_sl, undo_redo_system, world_system, selection_state_entity.guid->guid);
	
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	for (auto& request : ms_io->Requests) {
		u32 selection_state = 0;
		if (request.Type == ImGuiSelectionRequestType_SetAll) {
			if (request.Selected) {
				for (auto& entry : layers) {
					HashTableAddOrFind(selected_entities_hash_table, &world_system.heap, entry.guid);
				}
			} else {
				HashTableClear(selected_entities_hash_table);
			}
		} else if (request.Type == ImGuiSelectionRequestType_SetRange) {
			if (request.Selected) {
				for (auto& entry : ArrayViewCreate(layers, request.RangeFirstItem, request.RangeLastItem + 1)) {
					HashTableAddOrFind(selected_entities_hash_table, &world_system.heap, entry.guid);
				}
			} else {
				for (auto& entry : ArrayViewCreate(layers, request.RangeFirstItem, request.RangeLastItem + 1)) {
					HashTableRemove(selected_entities_hash_table, entry.guid);
				}
			}
		}
	}
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	EndUndoRedoCommand(undo_redo_system, is_dragging);
}

void TerrainEditorWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity) {
	ImGui::Begin("Terrain Editor");
	defer{ ImGui::End(); };
	
	auto* layer_stack_entity_array = QueryEntityTypeArray<TerrainEditorLayerStackEntityType>(world_system);
	if (layer_stack_entity_array->count == 0) return;
	
	auto layer_stack_entity = QueryFirstEntityByType<TerrainEditorLayerStackEntityType>(world_system);
	
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::SliderInt("##MinFrequencyBand", &layer_stack_entity.build_state->min_frequency_band, TerrainEditorEqualizer::min_frequency_band, TerrainEditorEqualizer::max_frequency_band, "Frequency Cutoff: %d", ImGuiSliderFlags_AlwaysClamp);
	
	
	Array<TerrainEditorLayerEntry> layers;
	ArrayReserve(layers, alloc, 128);
	
	BuildVisibleTerrainLayerArray(alloc, layers, world_system, layer_stack_entity.guid->guid);
	
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	auto* ms_io = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnClickVoid | ImGuiMultiSelectFlags_BoxSelect1d, (s32)selected_entities_hash_table.count, (s32)layers.count);
	ApplyEntitySelectionRequests(ms_io, layers, world_system, undo_redo_system, selection_state_entity);
	
	
	u32  drop_to_index = u32_max;
	u32  add_to_index  = u32_max;
	auto line_position = ImVec2(0.f, 0.f);
	bool is_delivery   = false;
	bool is_mouse_cursor_below = false;
	
	Array<u32> parent_stack;
	ArrayReserve(parent_stack, alloc, 16);
	
	auto& style = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.f));
	for (u32 index = 0; index < layers.count; index += 1) {
		auto& layer_entry = layers[index];
		
		while (parent_stack.count != 0 && ArrayLastElement(parent_stack) != layer_entry.parent_index) {
			ArrayPopLast(parent_stack);
			ImGui::Unindent();
		}
		
		auto layer = QueryEntityByGUID<TerrainEditorLayerQuery>(world_system, layer_entry.guid);
		ImGuiScopeID((void*)layer_entry.guid);
		
		auto entity_type_id = layer.array->entity_type_id;
		auto entity_type_name = entity_type_name_table[entity_type_id.index];
		
		auto tree_node_flags = (ImGuiTreeNodeFlags)ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (HashTableFind(selected_entities_hash_table, layer_entry.guid) != nullptr) {
			tree_node_flags |= ImGuiTreeNodeFlags_Selected;
		}
		
		if (layer.hierarchy->children.count == 0) {
			tree_node_flags |= ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf;
		}
		
		if (layer_entry.parent_index == u32_max) {
			tree_node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		
		auto layer_name = layer.name->name;
		auto display_name = layer_name.count ? layer_name : entity_type_name;
		
		ImGui::SetNextItemSelectionUserData(index);
		ImGui::SetNextItemStorageID((ImGuiID)layer_entry.guid);
		
		auto cursor_position_before = ImGui::GetCursorScreenPos();
		bool is_open = ImGui::TreeNodeEx(display_name.data, tree_node_flags);
		auto cursor_position_after  = ImGui::GetCursorScreenPos();
		
		compile_const char* payload_type_name = "TerrainLayer";
		if (ImGui::BeginDragDropSource()) {
			ImGui::TextUnformatted(display_name.data);
			ImGui::SetDragDropPayload(payload_type_name, nullptr, 0);
			ImGui::EndDragDropSource();
		}
		
		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload(payload_type_name, ImGuiDragDropFlags_AcceptPeekOnly)) {
				is_mouse_cursor_below = ImGui::GetMousePos().y > (cursor_position_before.y + cursor_position_after.y) * 0.5f;
				if ((is_mouse_cursor_below && is_open && ((tree_node_flags & ImGuiTreeNodeFlags_Leaf) == 0)) || ImGui::IsKeyDown(ImGuiMod_Shift)) {
					add_to_index  = index;
					line_position = cursor_position_after + ImVec2(style.IndentSpacing, 0.f);
				} else {
					add_to_index  = layer_entry.parent_index;
					line_position = is_mouse_cursor_below ? cursor_position_after : cursor_position_before;
				}
				drop_to_index = index;
				is_delivery   = payload->IsDelivery();
			}
			ImGui::EndDragDropTarget();
		}
		
		TerrainLayerCreationComboBox(undo_redo_system, world_system, selection_state_entity, layer);
		
		ImGui::Indent();
		ArrayAppend(parent_stack, alloc, index);
	}
	
	while (parent_stack.count != 0) {
		ArrayPopLast(parent_stack);
		ImGui::Unindent();
	}
	ImGui::PopStyleVar();
	
	if (is_delivery && add_to_index != u32_max) {
		ms_io->RangeSrcReset = true;
	}
	
	ms_io = ImGui::EndMultiSelect();
	ApplyEntitySelectionRequests(ms_io, layers, world_system, undo_redo_system, selection_state_entity);
	
	
	if (add_to_index != u32_max) {
		auto* draw_list = ImGui::GetWindowDrawList();
		
		auto p0 = line_position;
		auto p1 = ImVec2(ImGui::GetCursorScreenPos().x, line_position.y) + ImVec2(ImGui::GetContentRegionAvail().x, 0.f);
		
		draw_list->AddLine(p0, p1, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 1.f);
		draw_list->AddCircleFilled(p0, 3.f, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
		draw_list->AddCircleFilled(p1, 3.f, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
	}
	
	if (is_delivery && add_to_index != u32_max) {
		TempAllocationScope(alloc);
		u64 add_to_entity_guid = layers[add_to_index].guid;
		
		auto* storage = ImGui::GetStateStorage();
		storage->SetBool((ImGuiID)add_to_entity_guid, true);
		
		
		Array<u64> add_to_entity_hierarchy;
		ArrayReserve(add_to_entity_hierarchy, alloc, parent_stack.capacity);
		for (u32 index = add_to_index; index != u32_max; index = layers[index].parent_index) {
			ArrayAppend(add_to_entity_hierarchy, layers[index].guid);
		}
		
		// Only selected entities in the layers array should be considered. Preserving relative ordering of moved elements is also important.
		Array<u64> selected_roots;
		ArrayReserve(selected_roots, alloc, Math::Min(selected_entities_hash_table.count, layers.count));
		for (u32 index = 0, selected_parent = u32_max; index < layers.count; index += 1) {
			auto& layer_entry = layers[index];
			
			// If the parent is already selected, we don't need to move the current entity.
			if (selected_parent != u32_max && layer_entry.parent_index == selected_parent) continue;
			
			// Can't move the entity we're adding to, nor any of it's parents.
			bool is_selected =
				HashTableFind(selected_entities_hash_table, layer_entry.guid) != nullptr &&
				ArrayFind<u64>(add_to_entity_hierarchy, layer_entry.guid) == u64_max;
			
			if (is_selected) ArrayAppend(selected_roots, layer_entry.guid);
			selected_parent = is_selected ? index : u32_max;
		}
		
		
		auto add_to_entity = QueryEntityByGUID<HierarchyQuery>(world_system, add_to_entity_guid);
		auto& add_to_entity_children = add_to_entity.hierarchy->children;
		
		// We have to find drop_to_entry index before it's potentially removed from the add_to_entity_children array.
		u64 local_drop_to_index = 0;
		if (drop_to_index != add_to_index) {
			auto& drop_to_entry = layers[drop_to_index];
			local_drop_to_index = ArrayFind<GuidComponent>(add_to_entity_children, { drop_to_entry.guid }) + (is_mouse_cursor_below ? 1 : 0);
		}
		
		BeginUndoRedoGroup(undo_redo_system);
		for (u64 dropped_entity_guid : selected_roots) {
			auto dropped_entity = QueryEntityByGUID<HierarchyQuery>(world_system, dropped_entity_guid);
			u64 remove_from_entity_guid = dropped_entity.hierarchy->parent.guid;
			
			BeginUndoRedoCommand("Remove Child"_sl, undo_redo_system, world_system, remove_from_entity_guid);
			{
				auto remove_from_entity = QueryEntityByGUID<HierarchyQuery>(world_system, remove_from_entity_guid);
				auto& children = remove_from_entity.hierarchy->children;
				
				// TODO: We could store this index when we filter selected roots, and we could store runs of entities to be removed.
				u64 local_dropped_index = ArrayFind<GuidComponent>(children, { dropped_entity_guid });
				ArrayErase(children, local_dropped_index);
				
				local_drop_to_index -= (remove_from_entity_guid == add_to_entity_guid) && (local_dropped_index < local_drop_to_index) ? 1 : 0;
			}
			
			// Merge the two commands if they affect the same entity so no-op reorders don't generate undo/redo actions.
			if (remove_from_entity_guid != add_to_entity_guid) {
				EndUndoRedoCommand(undo_redo_system);
				
				BeginUndoRedoCommand("Change Parent"_sl, undo_redo_system, world_system, dropped_entity_guid);
				dropped_entity.hierarchy->parent.guid = add_to_entity_guid;
				EndUndoRedoCommand(undo_redo_system);
				
				BeginUndoRedoCommand("Add Child"_sl, undo_redo_system, world_system, add_to_entity_guid);
			}
			
			{
				// TODO: We could insert all of the selected roots at once after the the current loop instead of doing it here one by one.
				// But this would make it harder to ensure that no-op reorders don't generate any undo/redo actions.
				ArrayInsert(add_to_entity_children, &world_system.heap, local_drop_to_index, { dropped_entity_guid });
				local_drop_to_index += 1;
			}
			EndUndoRedoCommand(undo_redo_system);
		}
		EndUndoRedoGroup(undo_redo_system);
	}
}


static void TerrainEditorEqualizerWidget(const char* label, TerrainEditorEqualizer& equalizer) {
	ImGui::BeginGroup();
	ImGui::PushID(label);
	
	float width = ImGui::CalcItemWidth();
	float height = ImMin(width * 0.5f, ImGui::GetFrameHeight() * 8.f);
	ImGui::PushMultiItemsWidths(equalizer.frequency_band_count, width);
	
	auto& style = ImGui::GetStyle();
	for (s32 i = equalizer.min_frequency_band; i <= equalizer.max_frequency_band; i += 1) {
		ImGui::PushID(i);
		if (i > equalizer.min_frequency_band) {
			ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
		}
		
		char format_string[32] = {};
		ImFormatString(format_string, IM_ARRAYSIZE(format_string), "%d", i);
		
		ImGui::VSliderFloat("", ImVec2(ImGui::CalcItemWidth(), height), &equalizer[i], 0.f, 1.f, format_string);
		ImGui::SetItemTooltip("%.3f", equalizer[i]);
		
		ImGui::PopID();
		ImGui::PopItemWidth();
	}
	
	compile_const char* terrain_editor_equalizer_preset_names[(u32)TerrainEditorEqualizerPreset::Count] = {
		"None",
		"Neutral",
		"Low Pass",
		"High Pass",
		"Linear",
		"Inverse Linear",
		"V Shaped",
		"Inverse V Shaped",
		"U Shaped",
		"Inverse U Shaped",
	};
	
	if (ImGui::BeginCombo("##Preset", "Preset", ImGuiComboFlags_WidthFitPreview)) {
		for (u32 i = (u32)TerrainEditorEqualizerPreset::Neutral; i < (u32)TerrainEditorEqualizerPreset::Count; i += 1) {
			ImGuiScopeID(i);
			
			if (ImGui::Selectable(terrain_editor_equalizer_preset_names[i], false)) {
				equalizer = TerrainEditorEqualizer::MakePreset((TerrainEditorEqualizerPreset)i, equalizer.scale);
			}
		}
		ImGui::EndCombo();
	}
	
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::SliderFloat("##OverallScale", &equalizer.scale, 0.f, 1.f, "Scale: %.3f");
	
	ImGui::PopID();
	ImGui::EndGroup();
}

void TableTerrainEditorEqualizerWidget(const char* label, TerrainEditorEqualizer& equalizer) {
	if (ImGui::BeginTableItem(label)) {
		// Would be nice to span all columns of the table, but it doesn't seem like there is a way to do it in ImGui
		// without changing how the property editor works. We would need to end the current table and start a new one.
		TerrainEditorEqualizerWidget("", equalizer);
		
		ImGui::EndTableItem();
	}
}
