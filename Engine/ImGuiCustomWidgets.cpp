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

bool ImGui::InputText(const char* label, String& string, HeapAllocator& heap, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data) {
	InputTextHeapStringCallbackData callback_data;
	callback_data.string    = string;
	callback_data.capacity  = HeapAllocator::GetMemoryBlockSize(string.data); // Including null terminator.
	callback_data.heap      = &heap;
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
