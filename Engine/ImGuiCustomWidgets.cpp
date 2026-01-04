#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "ImGuiCustomWidgets.h"

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
			u64 new_capacity = Max(data->BufTextLen + 1, callback_data.capacity * 3 / 2);
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
	InputTextHeapStringCallbackData callback_data;
	callback_data.string    = string;
	callback_data.capacity  = HeapAllocator::GetMemoryBlockSize(string.data); // Including null terminator.
	callback_data.heap      = heap;
	callback_data.callback  = callback;
	callback_data.user_data = user_data;
	
	// ImGui doesn't like when a (null, 0) string buffer is passed in.
	// As a workaround supply a dummy ('\0', 1) buffer which will never be written to.
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
	
	compile_const u32 col_button[4]         = { IM_COL32(220,20,20,255), IM_COL32(20,180,20,255), IM_COL32(20,20,220,255), IM_COL32(140,140,140,255) };
	compile_const u32 col_button_hovered[4] = { IM_COL32(235,20,20,255), IM_COL32(20,195,20,255), IM_COL32(20,20,235,255), IM_COL32(160,160,160,255) };
	compile_const u32 col_button_active[4]  = { IM_COL32(250,20,20,255), IM_COL32(20,210,20,255), IM_COL32(20,20,250,255), IM_COL32(180,180,180,255) };
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
		
		ImGui::PushStyleColor(ImGuiCol_Button, col_button[i]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_button_hovered[i]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, col_button_active[i]);
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

