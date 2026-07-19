#include "Basic/Basic.h"
#include "LevelEditor.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "Engine/UndoRedoSystem.h"

#include <SDK/imgui/imgui_internal.h>

static float4 CameraEntityViewToClip(CameraEntityType camera_entity, float2 window_size) {
	float vertical_fov_degrees = camera_entity.camera->vertical_fov_degrees;
	float near_depth           = camera_entity.camera->near_depth;
	
	float4 view_to_clip_coef;
	if (camera_entity.camera->transform_type == CameraTransformType::Perspective) {
		view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
	} else {
		view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
	}
	
	return view_to_clip_coef;
}

static void CameraControls(CameraEntityType camera_entity, bool scene_focused, bool scene_hovered, ImGuiMouseButton locked_mouse_button, float2 mouse_uv, float2 window_size) {
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
	
	if (scene_hovered && io.MouseWheel != 0.f && locked_mouse_button == ImGuiMouseButton_COUNT) {
		auto view_to_clip_coef = CameraEntityViewToClip(camera_entity, window_size);
		auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
		
		auto view_space_ray = Math::RayInfoFromScreenUv(mouse_uv, clip_to_view_coef);
		
		float meters_per_click = 1.f;
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		float move_distance = io.MouseWheel * meters_per_click * sensetivity_scale;
		world_space_position += (view_to_world_quat * view_space_ray.direction) * move_distance;
	}
	
	if (locked_mouse_button == ImGuiMouseButton_Left && (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f)) {
		float radians_per_pixel = 1.f / 240.f;
		
		compile_const float3 view_space_up = float3(0.f, -1.f, 0.f);
		auto world_space_up = view_to_world_quat * view_space_up;
		view_to_world_quat = Math::AxisAngleToQuat(float3(0.f, 0.f, world_space_up.z < 0.f ? -1.f : 1.f), -io.MouseDelta.x * radians_per_pixel) * view_to_world_quat;
		
		// Use view_to_world_quat after we applied rotation around Z axis to it.
		view_to_world_quat = Math::AxisAngleToQuat(view_to_world_quat * float3(1.f, 0.f, 0.f), -io.MouseDelta.y * radians_per_pixel) * view_to_world_quat;
		
		view_to_world_quat = Math::Normalize(view_to_world_quat);
	} else if (locked_mouse_button == ImGuiMouseButton_Right && (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f)) {
		float meters_per_pixel = 1.f / 240.f;
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		auto world_to_view = Math::QuatToRotationMatrix(Math::Conjugate(view_to_world_quat));
		world_space_position += world_to_view.r0 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.x);
		world_space_position += world_to_view.r1 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.y);
	}
}

static bool GizmoControls(CameraEntityType camera_entity, WorldEntitySystem& world_system, UndoRedoSystem& undo_redo_system, HashTable<u64, void>& selected_entities_hash_table, float2 mouse_uv, float2 window_size) {
	if (selected_entities_hash_table.count != 1) return false;
	
	u64 entity_guid = (*selected_entities_hash_table.begin()).key;
	if (entity_guid == camera_entity.guid->guid) return false;
	
	auto typed_entity_id = FindEntityByGUID(world_system, entity_guid);
	auto* array = &world_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	auto entity = ExtractComponentStreams<TransformComponentQuery>(array, typed_entity_id.entity_id);
	if (entity.position == nullptr || entity.rotation == nullptr) return false;
	
	auto view_to_clip_coef = CameraEntityViewToClip(camera_entity, window_size);
	auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
	
	auto view_space_ray  = Math::RayInfoFromScreenUv(mouse_uv, clip_to_view_coef);
	auto world_space_ray = Math::TransformRayViewToWorld(view_space_ray, camera_entity.position->position, camera_entity.rotation->rotation);
	
	auto* draw_list_3d = ImGui::GetWindowDrawList3D();
	draw_list_3d->mouse_ray = world_space_ray;
	
	ImGui::PushScalingOrigin3D(entity.position->position, camera_entity.position->position, view_to_clip_coef, window_size.x, 96.f);
	defer{ ImGui::PopScalingOrigin3D(); };
	
	auto* storage = ImGui::GetStateStorage();
	auto& use_local_space_gizmo = *storage->GetBoolRef(ImGui::GetID("use_local_space_gizmo"));
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
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	bool is_dirty = EndUndoRedoCommand(undo_redo_system, is_dragging);
	
	if (is_dirty) {
		BitArraySetBit(array->dirty_mask, typed_entity_id.entity_id.index);
	}
	
	return is_dirty;
}

static void DrawDebugFrustumCullingBounds(ImGuiDrawList3D* draw_list_3d, CameraEntityType camera_entity, WorldEntityType world_entity) {
	auto& scene = world_entity.renderer_world->scene_constants;
	auto clip_to_view_coef = Math::ViewToClipInverse(scene.culling_view_to_clip_coef);
	auto world_space_position = scene.world_space_camera_position;
	auto view_to_world_quat = world_entity.renderer_world->debug_freeze_culling_camera.view_to_world_rotation;
	
	for (u32 i = 0; i < 4; i += 1) {
		auto uv_corner = float2((float)(i & 0x1), (float)((i >> 1) & 0x1));
		auto ray_info = Math::TransformRayViewToWorld(Math::RayInfoFromScreenUv(uv_corner, clip_to_view_coef), world_space_position, view_to_world_quat);
		draw_list_3d->AddArrow(ray_info.origin, ray_info.direction, 100.f, 0.1f, ~0u);
	}
}

void EditorViewportWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, u64 world_entity_guid, GraphicsContext* graphics_context) {
	auto world_entity  = QueryEntityByGUID<WorldEntityType>(world_system,  world_entity_guid);
	auto camera_entity = QueryEntityByGUID<CameraEntityType>(world_system, world_entity.camera_entity->guid);
	
	u32 scene_descriptor_heap_offset = AllocateTransientSrvDescriptorTable(graphics_context, 1);
	
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar);
	
	auto window_size = ImGui::GetWindowSize();
	auto window_pos  = ImGui::GetWindowPos();
	ImGui::SetNextItemAllowOverlap();
	ImGui::ImageButtonEx("Scene", scene_descriptor_heap_offset, window_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	bool scene_hovered = ImGui::IsItemHovered();
	bool scene_focused = ImGui::IsItemFocused();
	
	auto mouse_lock = ImGui::BeginMouseLock(scene_hovered, window_pos, window_pos + window_size);
	mouse_lock.Update(ImGuiMouseButton_Left);
	mouse_lock.Update(ImGuiMouseButton_Right);
	auto locked_mouse_button = ImGui::EndMouseLock(mouse_lock);
	
	auto window_relative_mouse_position = mouse_lock.GetMousePos() - window_pos;
	auto mouse_uv = float2(window_relative_mouse_position / window_size);
	CameraControls(camera_entity, scene_focused, scene_hovered, locked_mouse_button, mouse_uv, float2(window_size));
	
	ImGuiDrawList3D draw_list_3d;
	draw_list_3d.alloc = alloc;
	ImGui::SetWindowDrawList3D(&draw_list_3d);
	
	GizmoControls(camera_entity, world_system, undo_redo_system, world_selection_state_entity.selection_state->selected_entities_hash_table, mouse_uv, float2(window_size));
	
	if (world_entity.renderer_world->debug_freeze_culling_camera.enabled) {
		DrawDebugFrustumCullingBounds(&draw_list_3d, camera_entity, world_entity);
	}
	
	ImGui::End();
	ImGui::PopStyleVar(3);
	
	
	auto renderer_world = world_entity.renderer_world;
	renderer_world->window_size                  = float2(window_size);
	renderer_world->delta_time                   = ImGui::GetIO().DeltaTime;
	renderer_world->mouse_cursor_position        = s32x2(window_relative_mouse_position);
	renderer_world->debug_mesh_instance_arrays   = draw_list_3d.Flush();
	renderer_world->scene_descriptor_heap_offset = scene_descriptor_heap_offset;
}
