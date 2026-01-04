#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include <SDK/imgui/imgui.h>


struct GraphicsContext;
struct HeapAllocator;
struct SystemWindow;
struct String;

void ImGuiInitializeContext(HeapAllocator* heap);
void ImGuiInitializeWindow(SystemWindow* window);
void ImGuiReleaseContext(GraphicsContext* graphics_context);
void ImGuiBeginFrame(SystemWindow* window);


struct ImGuiMouseLock {
	ImGuiMouseButton locked_mouse_button = ImGuiMouseButton_COUNT;
	ImVec2 locked_mouse_pos;
	
	void Update(ImGuiMouseButton button, bool should_lock_mouse, ImVec2 inclusive_lock_rect_min, ImVec2 inclusive_lock_rect_max);
};

#define ImGuiScopeID(...) ImGui::PushID(__VA_ARGS__); defer{ ImGui::PopID(); }

namespace ImGui {
	bool InputText(const char* label, String& string, HeapAllocator* heap, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
	bool DragFloatWithReset(const char* label, float* data, u32 component_count, float v_speed = 1.f, float v_min = 0.f, float v_max = 0.f, const char* format = "%.3f", ImGuiSliderFlags flags = 0, const char* const* component_labels = nullptr, const float* default_values = nullptr);
	
	// Widgets for 2 column (Name, Data) tables:
	bool BeginTableItem(const char* label);
	void EndTableItem();
	
	bool TableInputText(const char* label, String& string, HeapAllocator* heap, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
	bool TableDragFloatWithReset(const char* label, float* data, u32 component_count, float v_speed = 1.f, float v_min = 0.f, float v_max = 0.f, const char* format = "%.3f", ImGuiSliderFlags flags = 0, const char* const* component_labels = nullptr, const float* default_values = nullptr);
	bool TableSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
	bool TableCombo(const char* label, s32* current_item, const char* items_separated_by_zeros, s32 popup_max_height_in_items = -1);
}
