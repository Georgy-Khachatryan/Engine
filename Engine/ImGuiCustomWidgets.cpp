#include "Basic/Basic.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "ImGuiCustomWidgets.h"
#include "Renderer/RendererEntities.h"

#include <SDK/imgui/imgui_internal.h>

static void ImGuiWrapMousePosition(ImVec2 inclusive_wrap_rect_min, ImVec2 inclusive_wrap_rect_max) {
	auto& io = ImGui::GetIO();
	auto* viewport = ImGui::GetWindowViewport();
	
	auto wrap_rect = ImRect(inclusive_wrap_rect_min, inclusive_wrap_rect_max - ImVec2(1.f, 1.f));
	wrap_rect.Floor();
	wrap_rect.ClipWithFull(ImRect(viewport->Pos, viewport->Pos + viewport->Size - ImVec2(1.f, 1.f)));
	
	auto mouse_pos = io.MousePos;
	for (u32 axis = 0; axis < 2; axis += 1) {
		if (mouse_pos[axis] >= wrap_rect.Max[axis]) {
			mouse_pos[axis] = wrap_rect.Min[axis] + 1.f;
		} else if (mouse_pos[axis] <= wrap_rect.Min[axis]) {
			mouse_pos[axis] = wrap_rect.Max[axis] - 1.f;
		}
	}
	
	if (mouse_pos.x != io.MousePos.x || mouse_pos.y != io.MousePos.y) {
		ImGui::TeleportMousePos(mouse_pos);
	}
}

void ImGuiMouseLock::Update(ImGuiMouseButton button, bool should_lock_mouse, ImVec2 inclusive_lock_rect_min, ImVec2 inclusive_lock_rect_max) {
	if (should_lock_mouse && locked_mouse_button == ImGuiMouseButton_COUNT && ImGui::IsMouseClicked(button)) {
		locked_mouse_button = button;
		locked_mouse_pos = ImGui::GetMousePos();
	}
	
	if (locked_mouse_button == button && ImGui::IsMouseDown(button) == false) {
		locked_mouse_button = ImGuiMouseButton_COUNT;
		ImGui::TeleportMousePos(locked_mouse_pos);
	}
	
	if (locked_mouse_button == button) {
		ImGuiWrapMousePosition(inclusive_lock_rect_min, inclusive_lock_rect_max);
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
	}
}


struct InputTextHeapStringCallbackData {
	String string;
	u64 capacity = 0;
	HeapAllocator* heap = nullptr;
	
	ImGuiInputTextCallback callback = nullptr;
	void* user_data = nullptr;
};

static s32 InputTextHeapStringCallback(ImGuiInputTextCallbackData* data) {
	auto& callback_data = *(InputTextHeapStringCallbackData*)data->UserData;
	
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		if (data->BufTextLen + 1 > callback_data.capacity) {
			u64 new_capacity = Math::Max((u64)data->BufTextLen + 1, callback_data.capacity * 3 / 2);
			callback_data.string.data = (char*)callback_data.heap->Reallocate(callback_data.string.data, callback_data.capacity, new_capacity);
			callback_data.capacity = new_capacity;
		}
		
		callback_data.string.count = data->BufTextLen;
		data->Buf = callback_data.string.data;
	} else if (callback_data.callback) {
		data->UserData = callback_data.user_data;
		callback_data.callback(data);
	}
	
	return 0;
}

bool ImGui::InputText(const char* label, String& string, HeapAllocator* heap, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data) {
	if (heap == nullptr) flags |= ImGuiInputTextFlags_ReadOnly;
	
	InputTextHeapStringCallbackData callback_data;
	callback_data.string    = string;
	callback_data.capacity  = heap ? heap->GetMemoryBlockSize(string.data) : string.count + 1; // Including null terminator.
	callback_data.heap      = heap;
	callback_data.callback  = callback;
	callback_data.user_data = user_data;
	
	// ImGui doesn't like when a (null, 0) string buffer is passed in.
	// As a workaround supply a dummy ('\0', 1) buffer which will never be written to.
	// This is similar to what ImGuiTextBuffer does.
	char dummy_string_data = '\0';
	
	bool result = ImGui::InputText(
		label,
		string.data ? string.data : &dummy_string_data,
		string.data ? callback_data.capacity : 1,
		flags | ImGuiInputTextFlags_CallbackResize,
		&InputTextHeapStringCallback,
		&callback_data
	);
	string = callback_data.string;
	
	return result;
}

// Based on ImGui::DragScalarN(...).
bool ImGui::DragFloatWithReset(const char* label, float* data, u32 component_count, float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags, const char* const* component_labels, const float* default_values) {
	auto* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;
	
	auto& style = ImGui::GetStyle();
	
	ImGui::BeginGroup();
	ImGui::PushID(label);
	
	ImGui::PushMultiItemsWidths(component_count, ImGui::CalcItemWidth());
	float frame_height = ImGui::GetFrameHeight();
	
	compile_const u32 col_button[4]         = { IM_COL32(220,20,20,255), IM_COL32(20,180,20,255), IM_COL32(20,20,220,255), IM_COL32(100,100,100,255) };
	compile_const u32 col_button_hovered[4] = { IM_COL32(235,20,20,255), IM_COL32(20,195,20,255), IM_COL32(20,20,235,255), IM_COL32(120,120,120,255) };
	compile_const u32 col_button_active[4]  = { IM_COL32(250,20,20,255), IM_COL32(20,210,20,255), IM_COL32(20,20,250,255), IM_COL32(140,140,140,255) };
	compile_const char* xyzw_component_labels[4] = { "X", "Y", "Z", "W" };
	compile_const float zero_default_values[4]   = { 0.f, 0.f, 0.f, 0.f };
	
	if (component_labels == nullptr) {
		component_labels = xyzw_component_labels;
	}
	
	if (default_values == nullptr) {
		default_values = zero_default_values;
	}
	
	bool value_changed = false;
	for (u32 i = 0; i < component_count; i += 1) {
		ImGui::PushID(i);
		
		if (i != 0) ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
		
		u32 color_index = component_count == 1 ? 3 : i;
		ImGui::PushStyleColor(ImGuiCol_Button, col_button[color_index]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_button_hovered[color_index]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, col_button_active[color_index]);
		if (ImGui::Button(component_labels[i], ImVec2(frame_height, frame_height))) {
			data[i] = default_values[i];
			value_changed = true;
		}
		ImGui::PopStyleColor(3);
		
		ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
		
		ImGui::SetNextItemWidth(ImMax(ImGui::CalcItemWidth() - frame_height - style.ItemInnerSpacing.x, 1.f));
		value_changed |= ImGui::DragFloat("", &data[i], v_speed, v_min, v_max, format, flags);
		
		ImGui::PopID();
		
		ImGui::PopItemWidth();
	}
	ImGui::PopID();
	
	const char* label_end = ImGui::FindRenderedTextEnd(label);
	if (label != label_end) {
		ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
		ImGui::TextEx(label, label_end);
	}
	
	ImGui::EndGroup();
	
	return value_changed;
}

bool ImGui::ColorEditN(const char* label, float* color, u32 component_count) {
	if (component_count == 4) return ImGui::ColorEdit4(label, color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	if (component_count == 3) return ImGui::ColorEdit3(label, color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	
	if (ImGui::ColorButton("##Color", ImVec4(*color, *color, *color, 1.f), ImGuiColorEditFlags_NoAlpha)) {
		ImGui::OpenPopup("GrayscalePicker");
		auto* context = ImGui::GetCurrentContext();
		ImGui::SetNextWindowPos(context->LastItemData.Rect.GetBL() + ImVec2(0.f, ImGui::GetStyle().ItemSpacing.y));
	}
	
	bool result = false;
	if (ImGui::BeginPopup("GrayscalePicker")) {
		result |= ImGui::SliderFloat("##GrayscalePicker", color, 0.f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndPopup();
	}
	
	return result;
}

bool ImGui::EntityComboBox(const char* label, EntitySystemBase* entity_system, u64* selected_guid, EntityTypeID entity_type_id) {
	auto* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;
	
	u64 current_guid = *selected_guid;
	const char* current_name = "Select Entity";
	
	auto* element = HashTableFind(entity_system->entity_guid_to_entity_id, current_guid);
	if (element) {
		auto typed_entity_id = element->value;
		auto* array = &entity_system->entity_type_arrays[typed_entity_id.entity_type_id.index];
		
		auto entity = ExtractComponentStreams<GuidNameQuery>(array, typed_entity_id.entity_id);
		if (entity.name->name.data) current_name = entity.name->name.data;
	}
	
	auto& style = ImGui::GetStyle();
	
	const char* button_label = "Clear";
	float button_width = ImGui::CalcTextSize(button_label).x + style.FramePadding.x * 2.f;
	float combo_width  = ImMax(ImGui::CalcItemWidth() - button_width - style.ItemInnerSpacing.x, 1.f);
	
	ImGui::SetNextItemWidth(combo_width);
	if (ImGui::BeginCombo(label, current_name)) {
		auto* array = &entity_system->entity_type_arrays[entity_type_id.index];
		auto streams = ExtractComponentStreams<GuidNameQuery>(array);
		
		auto entity_type_name = entity_type_name_table[entity_type_id.index];
		
		ImGuiListClipper clipper;
		clipper.Begin(array->count);
		
		while (clipper.Step()) {
			s32 index = 0;
			for (u64 i : BitArrayIt(array->alive_mask)) {
				defer{ index += 1; };
				if (index < clipper.DisplayStart) continue;
				if (index >= clipper.DisplayEnd) break;
				
				auto& [guid] = streams.guid[i];
				auto& [name] = streams.name[i];
				
				ImGuiScopeID((void*)guid);
				
				bool is_selected = (guid == current_guid);
				if (ImGui::Selectable(name.count ? name.data : entity_type_name.data, is_selected)) {
					*selected_guid = guid;
				}
			}
		}
		
		ImGui::EndCombo();
	}
	
	ImGui::EntityDragDropTarget(entity_type_id, selected_guid);
	
	ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
	if (ImGui::Button(button_label, ImVec2(button_width, 0.f))) {
		*selected_guid = 0;
	}
	
	return (current_guid != *selected_guid);
}

bool ImGui::EntityComboBoxWithColor(const char* label, EntitySystemBase* entity_system, float* color, u32 channel_count, u64* guid, EntityTypeID entity_type_id) {
	ImGuiScopeID(label);
	auto& style = ImGui::GetStyle();
	
	bool result = ImGui::ColorEditN("##Color", color, channel_count);
	ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
	
	result |= ImGui::EntityComboBox(label, entity_system, guid, entity_type_id);
	
	return result;
}

bool ImGui::EntityDragDropSource(EntityTypeID entity_type_id, u64 guid) {
	bool result = ImGui::BeginDragDropSource();
	if (result) {
		ImGui::Text("0x%llX", guid);
		
		char type_string[32] = {};
		ImFormatString(type_string, IM_ARRAYSIZE(type_string), "EntityTypeID:%X", entity_type_id.index);
		
		ImGui::SetDragDropPayload(type_string, &guid, sizeof(u64));
		ImGui::EndDragDropSource();
	}
	return result;
}

bool ImGui::EntityDragDropTarget(EntityTypeID entity_type_id, u64* guid) {
	u64 current_guid = *guid;
	if (ImGui::BeginDragDropTarget()) {
		char type_string[32] = {};
		ImFormatString(type_string, IM_ARRAYSIZE(type_string), "EntityTypeID:%X", entity_type_id.index);
		
		if (auto* payload = ImGui::AcceptDragDropPayload(type_string)) {
			memcpy(guid, payload->Data, sizeof(u64));
		}
	}
	return (current_guid != *guid);
}

bool ImGui::ImageButtonEx(const char* str_id, ImTextureRef tex_ref, const ImVec2& image_size, ImGuiButtonFlags flags) {
	auto* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;
	
	auto& style = ImGui::GetStyle();
	
	auto padding = style.FramePadding;
	ImRect bb(window->DC.CursorPos, window->DC.CursorPos + image_size + padding * 2.f);
	ImGui::ItemSize(bb);
	
	auto id = window->GetID(str_id);
	if (!ImGui::ItemAdd(bb, id)) return false;
	
	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);
	
	window->DrawList->AddImage(tex_ref, bb.Min + padding, bb.Max - padding);
	
	return pressed;
}

bool ImGui::BeginTableItem(const char* label) {
	ImGui::TableNextRow();
	ImGui::PushID(label);
	
	if (ImGui::TableSetColumnIndex(0)) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
	}
	
	bool result = ImGui::TableSetColumnIndex(1);
	if (result == false) ImGui::PopID();
	
	return result;
}

void ImGui::EndTableItem() {
	ImGui::PopID();
}

bool ImGui::TableInputText(const char* label, String& string, HeapAllocator* heap, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data) {
	bool result = false;
	if (ImGui::BeginTableItem(label)) {
		result = ImGui::InputText("", string, heap, flags, callback, user_data);
		ImGui::EndTableItem();
	}
	return result;
}

bool ImGui::TableDragFloatWithReset(const char* label, float* data, u32 component_count, float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags, const char* const* component_labels, const float* default_values) {
	bool result = false;
	if (ImGui::BeginTableItem(label)) {
		result = ImGui::DragFloatWithReset("", data, component_count, v_speed, v_min, v_max, format, flags, component_labels, default_values);
		ImGui::EndTableItem();
	}
	return result;
}

bool ImGui::TableSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags) {
	bool result = false;
	if (ImGui::BeginTableItem(label)) {
		result = ImGui::SliderFloat("", v, v_min, v_max, format, flags);
		ImGui::EndTableItem();
	}
	return result;
}

bool ImGui::TableCombo(const char* label, s32* current_item, const char* items_separated_by_zeros, s32 popup_max_height_in_items) {
	bool result = false;
	if (ImGui::BeginTableItem(label)) {
		result = ImGui::Combo("", current_item, items_separated_by_zeros, popup_max_height_in_items);
		ImGui::EndTableItem();
	}
	return result;
}

bool ImGui::TableEntityComboBox(const char* label, EntitySystemBase* entity_system, u64* guid, EntityTypeID entity_type_id) {
	bool result = false;
	if (ImGui::BeginTableItem(label)) {
		result |= ImGui::EntityComboBox("", entity_system, guid, entity_type_id);
		ImGui::EndTableItem();
	}
	return result;
}

bool ImGui::TableEntityComboBoxWithColor(const char* label, EntitySystemBase* entity_system, float* color, u32 channel_count, u64* guid, EntityTypeID entity_type_id) {
	bool result = false;
	if (ImGui::BeginTableItem(label)) {
		result |= ImGui::EntityComboBoxWithColor("", entity_system, color, channel_count, guid, entity_type_id);
		ImGui::EndTableItem();
	}
	return result;
}


ImGuiContext3D* GImGui3D = nullptr;

void ImGui::CreateContext3D() { GImGui3D = IM_NEW(ImGuiContext3D)(); }
void ImGui::DestroyContext3D() { auto* context = GImGui3D; GImGui3D = nullptr; IM_DELETE(context); }
ImGuiContext3D* ImGui::GetCurrentContext3D() { auto* context = GImGui3D; DebugAssert(context, "Missing ImGuiContext3D"); return context; }

void ImGui::SetWindowDrawList3D(ImGuiDrawList3D* draw_list_3d) {
	auto* window  = ImGui::GetCurrentWindow();
	auto* storage = ImGui::GetStateStorage();
	storage->SetVoidPtr(ImGui::GetIDWithSeed("ImGuiDrawList3D", nullptr, window->ID), draw_list_3d);
}

ImGuiDrawList3D* ImGui::GetWindowDrawList3D() {
	auto* window  = ImGui::GetCurrentWindow();
	auto* storage = ImGui::GetStateStorage();
	auto* draw_list_3d = (ImGuiDrawList3D*)storage->GetVoidPtr(ImGui::GetIDWithSeed("ImGuiDrawList3D", nullptr, window->ID));
	DebugAssert(draw_list_3d, "Missing ImGuiDrawList3D");
	
	return draw_list_3d;
}

void ImGui::PushScalingOrigin3D(const float3& scaling_origin, const float3& camera_position, const float4& view_to_clip_coef, float render_target_size_x, float widget_to_pixel_scale) {
	float denominator = view_to_clip_coef.x * render_target_size_x * 0.5f;
	float numerator   = 1.f;
	
	if (Math::IsPerspectiveMatrix(view_to_clip_coef)) {
		numerator = Math::Length(scaling_origin - camera_position);
	}
	
	float scale = widget_to_pixel_scale * numerator / Math::Max(denominator, 0.01f);
	
	auto* draw_list_3d = ImGui::GetWindowDrawList3D();
	ArrayAppend(draw_list_3d->scale_stack, draw_list_3d->scale);
	draw_list_3d->scale = scale;
}

void ImGui::PopScalingOrigin3D() {
	auto* draw_list_3d = ImGui::GetWindowDrawList3D();
	draw_list_3d->scale = ArrayPopLast(draw_list_3d->scale_stack);
}

bool ImGui::DragVector3D(const char* label, float3& position, const float3& offset, float3 direction, float length, float radius, u32 color) {
	auto* draw_list_3d = ImGui::GetWindowDrawList3D();
	auto* context = ImGui::GetCurrentContext3D();
	
	bool is_item_active = ImGui::GetID(label) == ImGui::GetActiveID();
	if (is_item_active == false && context->hide_inactive_widgets) return false;
	
	auto scaled_offset = offset * draw_list_3d->scale;
	auto scaled_length = length * draw_list_3d->scale;
	auto scaled_radius = radius * draw_list_3d->scale;
	
	auto& ray = draw_list_3d->mouse_ray;
	auto hit_result = Math::RayCylinderIntersect(ray, position + scaled_offset, position + scaled_offset + direction * (scaled_length * 1.25f), 0.f, scaled_radius * 2.f);
	
	bool result = false;
	bool hovered = false;
	
	auto& min_hit_distance = draw_list_3d->min_hit_distance;
	if (hit_result.hit && (hit_result.hit_distance < min_hit_distance) || is_item_active) {
		min_hit_distance = hit_result.hit_distance;
		
		ImGui::SetCursorScreenPos(ImGui::GetMousePos());
		ImGui::SetNextItemAllowOverlap();
		result = ImGui::InvisibleButton(label, ImVec2(1.f, 1.f), ImGuiButtonFlags_PressedOnClick);
		
		auto compute_drag_time = [&]()-> float {
			// Build a plane passing through direction and perpendicular to ray.direction.
			auto bitangent = Math::Cross(ray.direction, context->initial_direction);
			
			compile_const float eps = 1.f / (1024.f * 1024.f);
			if (Math::LengthSquare(bitangent) < eps) return 0.f;
			
			auto normal = Math::Normalize(Math::Cross(context->initial_direction, bitangent));
			
			auto hit_result = Math::RayPlaneIntersect(ray, normal, -Math::Dot(context->initial_position, normal));
			if (hit_result.hit == false || hit_result.hit_distance <= 0.f) return 0.f;
			
			return Math::Dot(ray.origin + ray.direction * hit_result.hit_distance - context->initial_position, context->initial_direction);
		};
		
		if (ImGui::IsItemActivated()) {
			context->initial_position  = position;
			context->initial_direction = direction;
			context->initial_offset    = offset;
			context->initial_time.x    = compute_drag_time();
		}
		
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			float time = compute_drag_time();
			position  = context->initial_position + context->initial_direction * (time - context->initial_time.x);
			direction = context->initial_direction;
			scaled_offset = context->initial_offset * draw_list_3d->scale;
		}
		
		hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
	}
	
	float hovered_scale = (hovered ? 1.125f : 1.f);
	draw_list_3d->AddArrow(position + scaled_offset, direction, scaled_length * hovered_scale, scaled_radius * hovered_scale, hovered ? 0xFF00FFFF : color);
	
	return result;
}

bool ImGui::DragPlane3D(const char* label, float3& position, const float3& offset, const float3& direction, const quat& rotation, const float3& half_extent, u32 color) {
	auto* draw_list_3d = ImGui::GetWindowDrawList3D();
	auto* context = ImGui::GetCurrentContext3D();
	
	bool is_item_active = ImGui::GetID(label) == ImGui::GetActiveID();
	if (is_item_active == false && context->hide_inactive_widgets) return false;
	
	auto scaled_offset = offset * draw_list_3d->scale;
	auto scaled_half_extent = half_extent * draw_list_3d->scale; 
	
	auto& ray = draw_list_3d->mouse_ray;
	auto hit_result = Math::RayBoxIntersect(ray, position + scaled_offset, rotation, scaled_half_extent * 1.125f);
	
	bool result = false;
	bool hovered = false;
	
	auto& min_hit_distance = draw_list_3d->min_hit_distance;
	if (hit_result.hit && (hit_result.hit_distance < min_hit_distance) || is_item_active) {
		min_hit_distance = hit_result.hit_distance;
		
		ImGui::SetCursorScreenPos(ImGui::GetMousePos());
		ImGui::SetNextItemAllowOverlap();
		result = ImGui::InvisibleButton(label, ImVec2(1.f, 1.f), ImGuiButtonFlags_PressedOnClick);
		
		auto compute_drag_time = [&]()-> float3 {
			auto drag_hit_result = Math::RayPlaneIntersect(ray, context->initial_direction, -Math::Dot(context->initial_position, context->initial_direction));
			if (drag_hit_result.hit == false || drag_hit_result.hit_distance <= 0.f) return 0.f;
			
			return ray.origin + ray.direction * drag_hit_result.hit_distance;
		};
		
		if (ImGui::IsItemActivated()) {
			context->initial_position  = position;
			context->initial_direction = direction;
			context->initial_offset    = offset;
			context->initial_time      = compute_drag_time();
		}
		
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			auto time = compute_drag_time();
			position = context->initial_position + (time - context->initial_time);
			scaled_offset = context->initial_offset * draw_list_3d->scale;
		}
		
		hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
	}
	
	float hovered_scale = (hovered ? 1.125f : 1.f);
	draw_list_3d->AddCube(position + scaled_offset, rotation, scaled_half_extent * hovered_scale, hovered ? 0xFF00FFFF : color);
	
	return result;
}

bool ImGui::DragKnob3D(const char* label, quat& rotation, const float3& position, const float3& direction, float major_radius, float minor_radius, u32 color) {
	auto* draw_list_3d = ImGui::GetWindowDrawList3D();
	auto* context = ImGui::GetCurrentContext3D();
	
	bool is_item_active = ImGui::GetID(label) == ImGui::GetActiveID();
	if (is_item_active == false && context->hide_inactive_widgets) return false;
	
	auto scaled_major_radius = major_radius * draw_list_3d->scale;
	auto scaled_minor_radius = minor_radius * draw_list_3d->scale;
	
	auto p0 = position - direction * (scaled_minor_radius * 1.125f);
	auto p1 = position + direction * (scaled_minor_radius * 1.125f);
	
	float r0 = scaled_major_radius - (scaled_minor_radius * 1.125f);
	float r1 = scaled_major_radius + (scaled_minor_radius * 1.125f);
	
	auto& ray = draw_list_3d->mouse_ray;
	auto hit_result = Math::RayCylinderIntersect(ray, p0, p1, r0, r1);
	
	bool result = false;
	bool hovered = false;
	
	auto& min_hit_distance = draw_list_3d->min_hit_distance;
	if (hit_result.hit && (hit_result.hit_distance < min_hit_distance) || is_item_active) {
		min_hit_distance = hit_result.hit_distance;
		
		ImGui::SetCursorScreenPos(ImGui::GetMousePos());
		ImGui::SetNextItemAllowOverlap();
		result = ImGui::InvisibleButton(label, ImVec2(1.f, 1.f), ImGuiButtonFlags_PressedOnClick);
		
		auto compute_drag_time = [&]()-> float {
			auto drag_hit_result = Math::RayPlaneIntersect(ray, context->initial_direction, -Math::Dot(context->initial_position, context->initial_direction));
			if (drag_hit_result.hit == false || drag_hit_result.hit_distance <= 0.f) return 0.f;
			
			auto world_to_tangent  = Math::BuildOrthonormalBasis(context->initial_direction);
			auto hit_tangent_space = world_to_tangent * (ray.origin - context->initial_position + ray.direction * drag_hit_result.hit_distance);
			
			return atan2f(hit_tangent_space.y, hit_tangent_space.x);
		};
		
		if (ImGui::IsItemActivated()) {
			context->initial_position  = position;
			context->initial_rotation  = rotation;
			context->initial_direction = direction;
			context->initial_time.x    = compute_drag_time();
		}
		
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			float time = compute_drag_time();
			rotation = Math::AxisAngleToQuat(context->initial_direction, time - context->initial_time.x) * context->initial_rotation;
			
			auto world_to_tangent = Math::BuildOrthonormalBasis(context->initial_direction);
			auto direction_0 = float3(cosf(context->initial_time.x), sinf(context->initial_time.x), 0.f) * world_to_tangent;
			auto direction_1 = float3(cosf(time), sinf(time), 0.f) * world_to_tangent;
			
			draw_list_3d->AddArrow(position, direction_0, scaled_major_radius - scaled_minor_radius * 2.f, scaled_minor_radius * 0.5f, color);
			draw_list_3d->AddArrow(position, direction_1, scaled_major_radius - scaled_minor_radius * 2.f, scaled_minor_radius * 0.5f, color);
		}
		
		hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
	}
	
	auto torus_rotation = Math::AxisAxisToQuat(float3(0.f, 0.f, 1.f), direction);
	float hovered_scale = (hovered ? 1.125f : 1.f);
	draw_list_3d->AddTorus(position, torus_rotation, scaled_major_radius, scaled_minor_radius * hovered_scale, hovered ? 0xFF00FFFF : color);
	
	return result;
}


void ImGuiDrawList3D::AddInstanceOfType(DebugMeshInstanceType instance_type, const float3& position, u32 color, const quat& rotation, const float4& packed_data) {
	auto& debug_mesh_instances = debug_mesh_instances_by_type[(u32)instance_type];
	if (debug_mesh_instances.count >= debug_mesh_instances.capacity) {
		if (debug_mesh_instances.count != 0) {
			ArrayAppend(debug_mesh_instance_arrays, alloc, { instance_type, debug_mesh_instances });
		}
		
		debug_mesh_instances = {};
		ArrayReserve(debug_mesh_instances, alloc, 128u);
	}
	
	auto& instance = ArrayEmplace(debug_mesh_instances);
	instance.position    = position;
	instance.color       = color;
	instance.rotation    = Math::EncodeR16G16B16A16_SNORM(float4(rotation));
	instance.packed_data = Math::EncodeR16G16B16A16_FLOAT(packed_data);
}

ArrayView<DebugMeshInstanceArray> ImGuiDrawList3D::Flush() {
	for (u32 instance_type = 0; instance_type < (u32)DebugMeshInstanceType::Count; instance_type += 1) {
		auto& debug_mesh_instances = debug_mesh_instances_by_type[instance_type];
		if (debug_mesh_instances.count != 0) {
			ArrayAppend(debug_mesh_instance_arrays, alloc, { (DebugMeshInstanceType)instance_type, debug_mesh_instances });
		}
	}
	
	auto result = debug_mesh_instance_arrays;
	debug_mesh_instances_by_type = {};
	debug_mesh_instance_arrays   = {};
	
	return result;
}

void ImGuiDrawList3D::AddSphere(const float3& position, float radius, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Sphere, position, color, quat{}, float4(radius, radius, radius, 0.f));
}

void ImGuiDrawList3D::AddSphere(const float3& position, const quat& rotation, const float3& radius, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Sphere, position, color, rotation, float4(radius, 0.f));
}

void ImGuiDrawList3D::AddCube(const float3& position, const quat& rotation, const float3& half_extent, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Cube, position, color, rotation, float4(half_extent, 0.f));
}

void ImGuiDrawList3D::AddCylinder(const float3& position, const quat& rotation, float height, float radius_0, float radius_1, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Cylinder, position, color, rotation, float4(height, radius_0, radius_1, 0.f));
}

void ImGuiDrawList3D::AddTorus(const float3& position, const quat& rotation, float major_radius, float minor_radius, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Torus, position, color, rotation, float4(major_radius, minor_radius, 0.f, 0.f));
}

void ImGuiDrawList3D::AddArrow(const float3& position, const float3& direction, float length, float radius, u32 color) {
	auto rotation = Math::AxisAxisToQuat(float3(0.f, 0.f, 1.f), direction);
	
	float arrow_head_radius  = radius * 2.f;
	float arrow_head_length  = arrow_head_radius * 3.f; // 3:1 arrow_head_length to arrow_head_diameter ratio.
	float arrow_shaft_length = Math::Max(length - arrow_head_length, 0.f);
	
	AddCylinder(position, rotation, arrow_shaft_length, radius, radius, color);
	AddCylinder(position + direction * arrow_shaft_length, rotation, arrow_head_length, arrow_head_radius, 0.f, color);
}

void ImGuiDrawList3D::AddArrow(const float3& from, const float3& to, float radius, u32 color) {
	auto direction = (to - from);
	auto length = Math::Length(direction);
	if (length == 0.f) return;
	
	AddArrow(from, direction * (1.f / length), length, radius, color);
}
