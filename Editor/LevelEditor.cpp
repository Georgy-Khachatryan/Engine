#include "Basic/Basic.h"
#include "Basic/BasicBitArray.h"
#include "Engine/Entities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"
#include "GraphicsApi/RecordContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderPasses.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/imgui_internal.h>

static void CameraControls(CameraEntityType camera_entity, bool scene_focused, bool scene_hovered, ImGuiMouseLock& mouse_lock, float2 window_pos, float2 window_size) {
	auto& view_to_world_quat   = camera_entity.rotation->rotation;
	auto& world_space_position = camera_entity.position->position;
	
	auto& io = ImGui::GetIO();
	
	if (scene_focused) {
		float base_speed = 10.f; // m/s
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		auto  world_to_view = Math::QuatToRotationMatrix(Math::Conjugate(view_to_world_quat));
		float move_distance = base_speed * sensetivity_scale * io.DeltaTime;
		if (ImGui::IsKeyDown(ImGuiKey_D, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r0 * +move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_A, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r0 * -move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_W, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r2 * +move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_S, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r2 * -move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_Q, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r1 * +move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_E, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r1 * -move_distance;
	}
	
	if (scene_hovered && io.MouseWheel != 0.f && mouse_lock.locked_mouse_button == ImGuiMouseButton_COUNT) {
		float vertical_fov_degrees = camera_entity.camera->vertical_fov_degrees;
		float near_depth           = camera_entity.camera->near_depth;
		
		float4 view_to_clip_coef;
		if (camera_entity.camera->transform_type == CameraTransformType::Perspective) {
			view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
		} else {
			view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
		}
		auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
		
		auto uv = (float2(ImGui::GetMousePos()) - window_pos) / window_size;
		auto ray_info = Math::RayInfoFromScreenUv(uv, clip_to_view_coef);
		
		auto view_to_world = Math::QuatToRotationMatrix(view_to_world_quat);
		
		float meters_per_click = 1.f;
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		float move_distance = io.MouseWheel * meters_per_click * sensetivity_scale;
		world_space_position += (view_to_world * ray_info.direction) * move_distance;
	}
	
	if (mouse_lock.locked_mouse_button == ImGuiMouseButton_Left) {
		float radians_per_pixel = 1.f / 240.f;
		
		compile_const float3 view_space_up = float3(0.f, -1.f, 0.f);
		auto world_space_up = view_to_world_quat * view_space_up;
		view_to_world_quat = Math::AxisAngleToQuat(float3(0.f, 0.f, world_space_up.z < 0.f ? -1.f : 1.f), -io.MouseDelta.x * radians_per_pixel) * view_to_world_quat;
		
		// Compute view_to_world after we applied rotation around Z axis.
		auto view_to_world = Math::QuatToRotationMatrix(view_to_world_quat);
		view_to_world_quat = Math::AxisAngleToQuat(view_to_world * float3(1.f, 0.f, 0.f), -io.MouseDelta.y * radians_per_pixel) * view_to_world_quat;
		
		view_to_world_quat = Math::Normalize(view_to_world_quat);
	} else if (mouse_lock.locked_mouse_button == ImGuiMouseButton_Right) {
		float meters_per_pixel = 1.f / 240.f;
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		auto world_to_view = Math::QuatToRotationMatrix(Math::Conjugate(view_to_world_quat));
		world_space_position += world_to_view.r0 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.x);
		world_space_position += world_to_view.r1 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.y);
	}
}

static void GizmoControls(CameraEntityType camera_entity, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, HashTable<u64, void>& selected_entities_hash_table, ImGuiDrawList3D* draw_list_3d, float2 window_pos, float2 window_size, bool& use_local_space_gizmo) {
	if (selected_entities_hash_table.count != 1) return;
	
	ImGui::Begin("Scene");
	defer{ ImGui::End(); };
	
	ImGui::SetWindowDrawList3D(draw_list_3d);
	
	u64 entity_guid = (*selected_entities_hash_table.begin()).key;
	if (entity_guid == camera_entity.guid->guid) return;
	
	auto typed_entity_id = FindEntityByGUID(world_system, entity_guid);
	auto* array = &world_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	auto entity = ExtractComponentStreams<EntityEditorQuery>(array, typed_entity_id.entity_id);
	if (entity.position == nullptr || entity.rotation == nullptr) return;
	
	{
		float vertical_fov_degrees = camera_entity.camera->vertical_fov_degrees;
		float near_depth           = camera_entity.camera->near_depth;
		
		float4 view_to_clip_coef;
		if (camera_entity.camera->transform_type == CameraTransformType::Perspective) {
			view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, float2(window_size), near_depth);
		} else {
			view_to_clip_coef = Math::OrthographicViewToClip(float2(window_size) * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
		}
		auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
		
		auto view_space_ray  = Math::RayInfoFromScreenUv((float2(ImGui::GetMousePos()) - float2(window_pos)) / float2(window_size), clip_to_view_coef);
		auto world_space_ray = Math::TransformRayViewToWorld(view_space_ray, camera_entity.position->position, camera_entity.rotation->rotation);
		
		draw_list_3d->mouse_ray = world_space_ray;
		
		ImGui::PushScalingOrigin3D(entity.position->position, camera_entity.position->position, view_to_clip_coef, window_size.x, 96.f);
	}
	defer{ ImGui::PopScalingOrigin3D(); };
	
	
	if (ImGui::Shortcut(ImGuiKey_1)) use_local_space_gizmo = true;
	if (ImGui::Shortcut(ImGuiKey_2)) use_local_space_gizmo = false;
	
	
	BeginUndoRedoCommand("Transform Gizmo"_sl, undo_redo_system, world_system, entity_guid);
	
	auto active_id = ImGui::GetActiveID();
	bool is_any_item_active = false;
	for (u32 i = 0; i < 3; i += 1) {
		ImGuiScopeID(i);
		
		is_any_item_active |= (active_id == ImGui::GetID("PositionVector"));
		is_any_item_active |= (active_id == ImGui::GetID("PositionPlane"));
		is_any_item_active |= (active_id == ImGui::GetID("RotationKnob"));
	}
	ImGui::GetCurrentContext3D()->hide_inactive_widgets = is_any_item_active;
	
	auto& world_space_position = entity.position->position;
	auto& model_to_world_quat  = entity.rotation->rotation;
	auto model_to_world = Math::QuatToRotationMatrix(model_to_world_quat);
	auto flipped_world_to_model = use_local_space_gizmo ? Math::Transpose(model_to_world) : float3x3{};
	
	auto view_vector = camera_entity.camera->transform_type == CameraTransformType::Orthographic ? -draw_list_3d->mouse_ray.direction : (draw_list_3d->mouse_ray.origin - world_space_position);
	for (u32 i = 0; i < 3; i += 1) {
		auto& direction = flipped_world_to_model[i];
		if (Math::Dot(direction, view_vector) < 0.f) {
			direction = -direction;
		}
	}
	
	auto model_to_world_plane_rotation = (use_local_space_gizmo ? model_to_world_quat : quat{});
	for (u32 i = 0; i < 3; i += 1) {
		ImGuiScopeID(i);
		
		auto model_space_direction = float3(0.f, 0.f, 0.f);
		model_space_direction[i] = 1.f;
		
		auto model_space_plane_offset = float3(1.f, 1.f, 1.f);
		model_space_plane_offset[i] = 0.f;
		
		float3 world_space_direction    = (model_space_direction    * flipped_world_to_model);
		float3 world_space_plane_offset = (model_space_plane_offset * flipped_world_to_model) * 0.5f;
		float3 model_space_plane_size   = (model_space_plane_offset * 0.25f) + (model_space_direction * 0.0125f);
		
		u32 color = 0xE1000000u | (0xFFu << (i * 8));
		ImGui::DragVector3D("PositionVector", world_space_position, world_space_direction, world_space_direction, 0.75f, 0.05f, color);
		ImGui::DragPlane3D("PositionPlane", world_space_position, world_space_plane_offset, world_space_direction, model_to_world_plane_rotation, model_space_plane_size, color);
		ImGui::DragKnob3D("RotationKnob", model_to_world_quat, world_space_position, world_space_direction, 1.25f, 0.05f, color);
	}
	draw_list_3d->AddSphere(entity.position->position, 0.1f * draw_list_3d->scale, 0xCCFFFFFF);
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused();
	bool is_dirty = EndUndoRedoCommand(undo_redo_system, world_system, is_dragging);
	
	if (is_dirty) {
		BitArraySetBit(array->dirty_mask, typed_entity_id.entity_id.index);
	}
}

static void DuplicateSelectedEntities(StackAllocator* alloc, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, HashTable<u64, void>& selected_entities_hash_table, u64 selection_state_entity) {
	TempAllocationScope(alloc);
	
	SaveLoadBuffer buffer;
	OpenSaveLoadBuffer(alloc, String{}, false, buffer);
	buffer.heap = &world_system.heap;
	
	for (auto [guid] : selected_entities_hash_table) {
		auto typed_entity_id = FindEntityByGUID(world_system, guid);
		auto* entity_array = &world_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
		SaveLoadEntityForTooling(buffer, entity_array, typed_entity_id.entity_id);
	}
	
	ResetSaveLoadBuffer(buffer, 0);
	
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
	
	
	BeginUndoRedoCommand("Select Duplicated Entities"_sl, undo_redo_system, world_system, selection_state_entity);
	HashTableClear(selected_entities_hash_table);
	for (u64 guid : new_entity_guids) {
		HashTableAddOrFind(selected_entities_hash_table, guid);
	}
	EndUndoRedoCommand(undo_redo_system, world_system);
	
	EndUndoRedoGroup(undo_redo_system);
}

static void RemoveSelectedEntities(WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, HashTable<u64, void>& selected_entities_hash_table, u64 selection_state_entity, u64 camera_entity_guid) {
	BeginUndoRedoGroup(undo_redo_system);
	HashTableRemove(selected_entities_hash_table, camera_entity_guid); // Don't remove the active camera.
	
	for (auto& [guid] : selected_entities_hash_table) {
		UndoRedoRemoveEntity(undo_redo_system, world_system, guid);
		RemoveEntityByGUID(world_system, guid);
	}
	
	BeginUndoRedoCommand("Deselect Removed Entities"_sl, undo_redo_system, world_system, selection_state_entity);
	HashTableClear(selected_entities_hash_table);
	EndUndoRedoCommand(undo_redo_system, world_system);
	
	EndUndoRedoGroup(undo_redo_system);
}

static void ApplyEntitySelectionRequests(ImGuiMultiSelectIO* ms_io, ArrayView<EntityTypeArray*> entity_view, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, HashTable<u64, void>& selected_entities_hash_table, u64 world_entity_guid) {
	BeginUndoRedoCommand("Select Entities"_sl, undo_redo_system, world_system, world_entity_guid);
	
	for (auto& request : ms_io->Requests) {
		if (request.Type == ImGuiSelectionRequestType_SetAll) {
			if (request.Selected) {
				for (auto* entity_array : entity_view) {
					auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
					for (u64 i : BitArrayIt(entity_array->alive_mask)) {
						auto& [guid] = streams.guid[i];
						HashTableAddOrFind(selected_entities_hash_table, &world_system.heap, guid);
					}
				}
			} else {
				HashTableClear(selected_entities_hash_table);
			}
		} else if (request.Type == ImGuiSelectionRequestType_SetRange) {
			u32 global_index = 0;
			for (auto* entity_array : entity_view) {
				u32 start_index = Max((u32)request.RangeFirstItem, global_index);
				u32 end_index   = Min((u32)request.RangeLastItem + 1, global_index + entity_array->count);
				defer{ global_index += entity_array->count; };
				
				if (start_index >= end_index) continue;
				
				auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
				u32 index = global_index;
				for (u64 i : BitArrayIt(entity_array->alive_mask)) {
					defer{ index += 1; };
					if (index < start_index) continue;
					if (index >= end_index) break;
					
					auto& [guid] = streams.guid[i];
					
					if (request.Selected) {
						HashTableAddOrFind(selected_entities_hash_table, &world_system.heap, guid);
					} else {
						HashTableRemove(selected_entities_hash_table, guid);
					}
				}
			}
		}
	}
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused();
	EndUndoRedoCommand(undo_redo_system, world_system, is_dragging);
}

static void LevelEditorSceneView(StackAllocator* alloc, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, u64 world_entity_guid) {
	ProfilerScope("SceneView");
	auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInner | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	if (!ImGui::BeginTable("SceneView", 3, flags)) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableHeadersRow();
	
	
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, world_entity_guid);
	auto& selected_entities_hash_table = world_entity.selection_state->selected_entities_hash_table;
	auto entity_view = QueryEntities<GuidNameQuery>(alloc, world_system);
	
	u32 entity_count = 0;
	for (auto* entity_array : entity_view) {
		entity_count += entity_array->count;
	}
	
	
	auto* ms_io = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnClickVoid | ImGuiMultiSelectFlags_BoxSelect1d, (s32)selected_entities_hash_table.count, (s32)entity_count);
	ApplyEntitySelectionRequests(ms_io, entity_view, world_system, undo_redo_system, selected_entities_hash_table, world_entity_guid);
	
	
	ImGuiListClipper clipper;
	clipper.Begin(entity_count);
	if (ms_io->RangeSrcItem != -1) clipper.IncludeItemByIndex((s32)ms_io->RangeSrcItem);
	
	ImGui::PushStyleColor(ImGuiCol_NavCursor, 0u);
	while (clipper.Step()) {
		u32 global_index = 0;
		for (auto* entity_array : entity_view) {
			u32 start_index = Max(clipper.DisplayStart, global_index);
			u32 end_index   = Min(clipper.DisplayEnd,   global_index + entity_array->count);
			defer{ global_index += entity_array->count; };
			
			if (start_index >= end_index) continue;
			
			auto entity_type_name = entity_type_name_table[entity_array->entity_type_id.index];
			
			auto streams = ExtractComponentStreams<GuidNameQuery>(entity_array);
			u32 index = global_index;
			for (u64 i : BitArrayIt(entity_array->alive_mask)) {
				defer{ index += 1; };
				if (index < start_index) continue;
				if (index >= end_index) break;
				
				auto& [guid] = streams.guid[i];
				auto& [name] = streams.name[i];
				
				
				ImGui::TableNextRow();
				
				ImGuiScopeID((void*)guid);
				
				if (ImGui::TableSetColumnIndex(0)) {
					ImGui::Bullet();
					ImGui::SameLine();
					
					bool is_selected = HashTableFind(selected_entities_hash_table, guid) != nullptr;
					ImGui::SetNextItemSelectionUserData(index);
					ImGui::Selectable(name.count ? name.data : entity_type_name.data, is_selected, ImGuiSelectableFlags_SpanAllColumns);
					
					if (ImGui::BeginPopupContextItem()) {
						if (entity_array->entity_type_id.index == ECS::GetEntityTypeID<CameraEntityType>::id.index) {
							if (ImGui::Selectable("Set Active Camera")) {
								BeginUndoRedoCommand("Set Active Camera"_sl, undo_redo_system, world_system, world_entity_guid);
								world_entity.camera_entity->guid = guid;
								EndUndoRedoCommand(undo_redo_system, world_system);
							}
						}
						
						if (ImGui::Selectable("Delete")) {
							RemoveSelectedEntities(world_system, undo_redo_system, selected_entities_hash_table, world_entity_guid, world_entity.camera_entity->guid);
						}
						
						if (ImGui::Selectable("Duplicate")) {
							DuplicateSelectedEntities(alloc, world_system, undo_redo_system, selected_entities_hash_table, world_entity_guid);
						}
						
						ImGui::EndPopup();
					}
					
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						if (entity_array->entity_type_id.index == ECS::GetEntityTypeID<CameraEntityType>::id.index) {
							BeginUndoRedoCommand("Select Active Camera"_sl, undo_redo_system, world_system, world_entity_guid);
							world_entity.camera_entity->guid = guid;
							EndUndoRedoCommand(undo_redo_system, world_system);
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
	}
	ImGui::PopStyleColor();
	
	ms_io = ImGui::EndMultiSelect();
	ApplyEntitySelectionRequests(ms_io, entity_view, world_system, undo_redo_system, selected_entities_hash_table, world_entity_guid);
}


static void LevelEditorEntityView(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, UndoRedoSystem& undo_redo_system, u64 world_entity_guid) {
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, world_entity_guid);
	auto& selected_entities_hash_table = world_entity.selection_state->selected_entities_hash_table;
	
	ImGui::Begin("Entity Editor", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
	defer{ ImGui::End(); };
	
	auto table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	
	if (ImGui::BeginTable("Components", 2, table_flags) == false) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableHeadersRow();
	
	if (selected_entities_hash_table.count == 0) return;
	
	
	// Items should span full width of the column.
	ImGui::PushItemWidth(-FLT_MIN);
	defer{ ImGui::PopItemWidth(); };
	
	u64 entity_guid = (*selected_entities_hash_table.begin()).key;
	ImGuiScopeID((void*)entity_guid);
	
	ImGui::BeginDisabled(selected_entities_hash_table.count >= 2);
	defer{ ImGui::EndDisabled(); };
	
	auto typed_entity_id = FindEntityByGUID(world_system, entity_guid);
	auto* array = &world_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	auto entity = ExtractComponentStreams<EntityEditorQuery>(array, typed_entity_id.entity_id);
	
	
	BeginUndoRedoCommand("Entity Editor"_sl, undo_redo_system, world_system, entity_guid);
	
	if (entity.guid) {
		auto guid_string = StringFormat(alloc, "0x%"_sl, (void*)entity.guid->guid);
		ImGui::TableInputText("GUID", guid_string, nullptr);
	}
	
	if (entity.name) {
		auto& name = entity.name->name;
		ImGui::TableInputText("Name", name, &world_system.heap);
	}
	
	if (entity.position) {
		auto& position = entity.position->position;
		ImGui::TableDragFloatWithReset("Position", &position.x, 3, 0.1f);
	}
	
	if (entity.rotation) {
		auto& rotation = entity.rotation->rotation;
		
		auto euler_angles = Math::QuatToEulerXyzAngles(rotation) * Math::radians_to_degress;
		if (ImGui::TableDragFloatWithReset("Rotation", &euler_angles.x, 3, 1.f)) {
			rotation = Math::EulerXyzAnglesToQuat(euler_angles * Math::degrees_to_radians);
		}
	}
	
	if (entity.scale) {
		const char* label = "S";
		const float default_values = 1.f;
		ImGui::TableDragFloatWithReset("Scale", &entity.scale->scale, 1, 0.1f, 0.f, 8.f, "%.3f", 0, &label, &default_values);
	}
	
	if (entity.mesh_asset) {
		ImGui::TableEntityComboBox("Mesh Asset", &asset_system, &entity.mesh_asset->guid, ECS::GetEntityTypeID<MeshAssetType>::id);
	}
	
	if (entity.camera) {
		ImGui::TableCombo("Camera Transform Type", (s32*)&entity.camera->transform_type, "Perspective\0Orthographic\0");
		ImGui::TableSliderFloat("Vertical Field Of View", &entity.camera->vertical_fov_degrees, 10.f, 135.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::TableSliderFloat("Camera Near Depth", &entity.camera->near_depth, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	}
	
	if (entity.mesh_source_data) {
		ImGui::TableInputText("Mesh Source Data", entity.mesh_source_data->filepath, nullptr);
	}
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused();
	bool is_dirty = EndUndoRedoCommand(undo_redo_system, world_system, is_dragging);
	
	if (is_dirty) {
		BitArraySetBit(array->dirty_mask, typed_entity_id.entity_id.index);
	}
}

static void DrawUndoRedoCommandRow(UndoRedoCommand& command) {
	compile_const char* command_type_names[3] = { "LoadEntityState", "CreateEntity", "RemoveEntity" };
	
	ImGui::TableNextRow();
	if (ImGui::TableSetColumnIndex(0)) ImGui::Text("0x%016llX", command.entity_guid);
	if (ImGui::TableSetColumnIndex(1)) ImGui::Text("%u", command.group_index);
	if (ImGui::TableSetColumnIndex(2)) ImGui::Text("%llu", command.offset);
	if (ImGui::TableSetColumnIndex(3)) ImGui::Text("%llu", command.size);
	if (ImGui::TableSetColumnIndex(4)) ImGui::TextUnformatted(command_type_names[(u32)command.command_type]);
}

static void LevelEditorUndoRedoHistoryView(UndoRedoSystem& undo_redo_system) {
	ImGui::Begin("Undo/Redo History");
	defer{ ImGui::End(); };
	
	ImGui::Text("Undo/Redo Buffer Size: %llu", undo_redo_system.undo_buffer.save_load_buffer.data.count + undo_redo_system.redo_buffer.save_load_buffer.data.count);
	
	auto table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("Components", 5, table_flags) == false) return;
	defer{ ImGui::EndTable(); };
		
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("GUID",    ImGuiTableColumnFlags_WidthStretch, 4.f);
	ImGui::TableSetupColumn("GroupID", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("Offset",  ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableSetupColumn("Size",    ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthStretch, 3.f);
	ImGui::TableHeadersRow();
	
	auto& undo_commands = undo_redo_system.undo_buffer.commands;
	for (s64 i = 0; i < (s64)undo_commands.count; i += 1) {
		DrawUndoRedoCommandRow(undo_commands[i]);
	}
	
	ImGui::TableNextRow();
	if (ImGui::TableSetColumnIndex(0)) {
		ImGui::CollapsingHeader("^^^ Undo Commands ^^^ | vvv Redo Commands vvv ", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns | ImGuiTreeNodeFlags_Leaf);
	}
	
	auto& redo_commands = undo_redo_system.redo_buffer.commands;
	for (s64 i = redo_commands.count - 1; i >= 0; i -= 1) {
		DrawUndoRedoCommandRow(redo_commands[i]);
	}
}

compile_const auto entities_save_load_path = "./Assets/Scene.csb"_sl;
compile_const auto assets_save_load_path   = "./Assets/Assets.csb"_sl;

static bool SaveLoadEntitySystemByPath(StackAllocator* alloc, EntitySystemBase& world_system, String filepath, bool is_loading) {
	TempAllocationScope(alloc);
	
	SaveLoadBuffer buffer;
	bool success = OpenSaveLoadBuffer(alloc, filepath, is_loading, buffer);
	
	if (success) {
		SaveLoadEntitySystem(buffer, world_system);
		CloseSaveLoadBuffer(buffer);
	}
	
	return success;
}

static void LoadOrCreateDefaultWorld(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system) {
	bool loaded_entities = SaveLoadEntitySystemByPath(alloc, world_system, entities_save_load_path, true);
	bool loaded_assets   = SaveLoadEntitySystemByPath(alloc, asset_system, assets_save_load_path, true);
	
	if (loaded_entities == false) {
		TempAllocationScope(alloc);
		
		auto world_entity  = CreateEntity<WorldEntityType>(world_system);
		auto camera_entity = CreateEntity<CameraEntityType>(world_system);
		
		camera_entity.rotation->rotation =
			Math::AxisAngleToQuat(float3(0.f, 0.f, 1.f), -90.f * Math::degrees_to_radians) *
			Math::AxisAngleToQuat(float3(1.f, 0.f, 0.f), -90.f * Math::degrees_to_radians);
		
		camera_entity.name->name = StringCopy(&world_system.heap, "DefaultCamera"_sl);
		world_entity.camera_entity->guid = camera_entity.guid->guid;
	}
}

struct LevelEditorContext {
	UndoRedoSystem undo_redo_system;
	
	ImGuiMouseLock mouse_lock;
	bool use_local_space_gizmo = true;
};

LevelEditorContext* CreateLevelEditorContext(StackAllocator* alloc, HeapAllocator* heap, WorldEntitySystem& world_system, AssetEntitySystem& asset_system) {
	auto* editor_context = NewFromAlloc(alloc, LevelEditorContext);
	InitializeUndoRedoSystem(editor_context->undo_redo_system, heap);
	
	LoadOrCreateDefaultWorld(alloc, world_system, asset_system);
	
	return editor_context;
}

void ReleaseLevelEditorContext(LevelEditorContext* editor_context) {
	ReleaseUndoRedoSystem(editor_context->undo_redo_system);
}

void LevelEditorUpdate(LevelEditorContext* editor_context, StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, u64 world_entity_guid) {
	ProfilerScope("LevelEditorUpdate");
	
	auto& undo_redo_system = editor_context->undo_redo_system;
	LevelEditorUndoRedoHistoryView(undo_redo_system);
	
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, world_entity_guid);
	auto& selected_entities_hash_table = world_entity.selection_state->selected_entities_hash_table;
	
	auto scene_radiance_resource_id = world_entity.renderer_world->enable_anti_aliasing ? VirtualResourceID::SceneRadianceResult : VirtualResourceID::SceneRadiance;
	u32 scene_descriptor_heap_offset = CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>(scene_radiance_resource_id));
	
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.f, 0.f));
	ImGui::Begin("Scene");
	
	auto window_size = ImGui::GetWindowSize();
	auto window_pos  = ImGui::GetWindowPos();
	ImGui::SetNextItemAllowOverlap();
	ImGui::ImageButtonEx("Scene", scene_descriptor_heap_offset, window_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	bool scene_hovered = ImGui::IsItemHovered();
	bool scene_focused = ImGui::IsItemFocused();
	
	auto mouse_lock_rect_min = window_pos;
	auto mouse_lock_rect_max = window_pos + window_size;
	auto& mouse_lock = editor_context->mouse_lock;
	mouse_lock.Update(ImGuiMouseButton_Left,  scene_hovered, mouse_lock_rect_min, mouse_lock_rect_max);
	mouse_lock.Update(ImGuiMouseButton_Right, scene_hovered, mouse_lock_rect_min, mouse_lock_rect_max);
	
	ImGui::End();
	ImGui::PopStyleVar(2);
	
	
	if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
		RemoveSelectedEntities(world_system, undo_redo_system, selected_entities_hash_table, world_entity_guid, world_entity.camera_entity->guid);
	}
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, ImGuiInputFlags_RouteGlobal)) {
		DuplicateSelectedEntities(alloc, world_system, undo_redo_system, selected_entities_hash_table, world_entity_guid);
	}
	
	if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal)) {
		BeginUndoRedoCommand("Deselect Entities"_sl, undo_redo_system, world_system, world_entity_guid);
		HashTableClear(selected_entities_hash_table);
		EndUndoRedoCommand(undo_redo_system, world_system);
	}
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat)) {
		ExecuteUndo(undo_redo_system, world_system);
	}
	
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat)) {
		ExecuteRedo(undo_redo_system, world_system);
	}
	
	
	ImGui::Begin("Outliner");
	ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
	bool should_save_scene = ImGui::Button("Save State");
	
	ImGui::SameLine();
	
	ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_L, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
	bool should_load_scene = ImGui::Button("Load State") && (should_save_scene == false);
	
	if (should_save_scene || should_load_scene) {
		SaveLoadEntitySystemByPath(alloc, world_system, entities_save_load_path, should_load_scene);
		
		if (should_save_scene) {
			SaveLoadEntitySystemByPath(alloc, asset_system, assets_save_load_path,   should_load_scene);
		}
		
		if (should_load_scene) {
			ResetUndoRedoSystem(undo_redo_system);
		}
	}
	
	ImGui::SliderFloat("Meshlet Target Error Pixels", &world_entity.renderer_world->meshlet_target_error_pixels, 1.f, 128.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Sun Elevation", &world_entity.renderer_world->sun_elevation_degrees, -10.f, +190.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::Checkbox("Enable Anti Aliasing", &world_entity.renderer_world->enable_anti_aliasing);
	
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##CreateEntity", "Create Entity")) {
		EntityTypeID entity_type_ids[] = {
			ECS::GetEntityTypeID<MeshEntityType>::id,
			ECS::GetEntityTypeID<CameraEntityType>::id,
		};
		
		for (auto entity_type_id : entity_type_ids) {
			auto name = entity_type_name_table[entity_type_id.index];
			
			ImGuiScopeID(entity_type_id.index);
			if (ImGui::Selectable(name.data, false)) {
				auto entity_id = CreateEntity(world_system, entity_type_id);
				auto entity = ExtractComponentStreams<GuidNameQuery>(&world_system.entity_type_arrays[entity_type_id.index], entity_id);
				entity.name->name = StringCopy(&world_system.heap, name);
				
				BeginUndoRedoGroup(undo_redo_system);
				UndoRedoCreateEntity(undo_redo_system, world_system, entity.guid->guid);
				
				BeginUndoRedoCommand("Select Created Entity"_sl, undo_redo_system, world_system, world_entity_guid);
				HashTableClear(selected_entities_hash_table);
				HashTableAddOrFind(selected_entities_hash_table, &world_system.heap, entity.guid->guid);
				EndUndoRedoCommand(undo_redo_system, world_system);
				
				EndUndoRedoGroup(undo_redo_system);
			}
		}
		ImGui::EndCombo();
	}
	
	LevelEditorSceneView(alloc, world_system, undo_redo_system, world_entity_guid);
	ImGui::End();
	
	LevelEditorEntityView(alloc, world_system, asset_system, undo_redo_system, world_entity_guid);;
	
	ImGuiDrawList3D draw_list_3d;
	draw_list_3d.alloc = alloc;
	
	auto camera_entity = QueryEntityByGUID<CameraEntityType>(world_system, world_entity.camera_entity->guid);
	CameraControls(camera_entity, scene_focused, scene_hovered, mouse_lock, float2(window_pos), float2(window_size));
	GizmoControls(camera_entity, world_system, undo_redo_system, selected_entities_hash_table, &draw_list_3d, float2(window_pos), float2(window_size), editor_context->use_local_space_gizmo);
	
	auto renderer_world = world_entity.renderer_world;
	renderer_world->window_size = float2(window_size);
	renderer_world->debug_mesh_instance_arrays = draw_list_3d.Flush();
}
