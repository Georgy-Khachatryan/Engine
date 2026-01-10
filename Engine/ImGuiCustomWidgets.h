#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include <SDK/imgui/imgui.h>


struct EntitySystem;
struct GraphicsContext;
struct HeapAllocator;
struct String;
struct SystemWindow;
struct EntityTypeID;

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
	bool EntityComboBox(const char* label, EntitySystem* entity_system, u64* guid, EntityTypeID entity_type_id);
	bool ImageButtonEx(const char* str_id, ImTextureRef tex_ref, const ImVec2& image_size, ImGuiButtonFlags flags = 0);
	
	// Widgets for 2 column (Name, Data) tables:
	bool BeginTableItem(const char* label);
	void EndTableItem();
	
	bool TableInputText(const char* label, String& string, HeapAllocator* heap, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
	bool TableDragFloatWithReset(const char* label, float* data, u32 component_count, float v_speed = 1.f, float v_min = 0.f, float v_max = 0.f, const char* format = "%.3f", ImGuiSliderFlags flags = 0, const char* const* component_labels = nullptr, const float* default_values = nullptr);
	bool TableSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
	bool TableCombo(const char* label, s32* current_item, const char* items_separated_by_zeros, s32 popup_max_height_in_items = -1);
	bool TableEntityComboBox(const char* label, EntitySystem* entity_system, u64* guid, EntityTypeID entity_type_id);
}


struct DebugMeshInstance;
struct DebugMeshInstanceArray;
enum struct DebugMeshInstanceType : u32;

struct ImGuiDrawList3D {
	StackAllocator* alloc = nullptr;
	
	FixedCountArray<Array<DebugMeshInstance>, 4> debug_mesh_instances_by_type;
	Array<DebugMeshInstanceArray> debug_mesh_instance_arrays;
	
	void AddSphere(const float3& position, float radius, u32 color);
	void AddSphere(const float3& position, const quat& rotation, const float3& radius, u32 color);
	void AddCube(const float3& position, const quat& rotation, const float3& half_extent, u32 color);
	void AddCylinder(const float3& position, const quat& rotation, float height, float radius_0, float radius_1, u32 color);
	void AddTorus(const float3& position, const quat& rotation, float major_radius, float minor_radius, u32 color);
	void AddArrow(const float3& position, const float3& direction, float length, float radius, u32 color);
	void AddArrow(const float3& from, const float3& to, float radius, u32 color);
	
	void AddInstanceOfType(DebugMeshInstanceType instance_type, const float3& position, u32 color, const quat& rotation, const float4& packed_data);
	
	ArrayView<DebugMeshInstanceArray> Flush();
};
