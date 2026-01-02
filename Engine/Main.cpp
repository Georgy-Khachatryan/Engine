#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicSaveLoad.h"
#include "Basic/BasicString.h"
#include "Entities.h"
#include "EntitySystem.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "RenderPasses.h"
#include "SystemWindow.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/imgui_internal.h>
#include <SDK/imgui/backends/imgui_impl_win32.h>


static void ImGuiSetCustomStyle() {
	auto& style = ImGui::GetStyle();
	style.WindowRounding = 4.f;
	style.ChildRounding  = 4.f;
	style.FrameRounding  = 4.f;
	style.PopupRounding  = 4.f;
	style.GrabRounding   = 2.f;
	
	auto colors = ArrayView<ImVec4>{ style.Colors, ArraySize(style.Colors) };
	colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.96f);
	colors[ImGuiCol_ChildBg]                = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_FrameBgActive]          = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_TitleBg]                = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_TitleBgActive]          = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_CheckMark]              = ImVec4(0.95f, 0.83f, 0.66f, 1.00f);
	colors[ImGuiCol_SliderGrab]             = ImVec4(0.70f, 0.61f, 0.47f, 1.00f);
	colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.95f, 0.83f, 0.66f, 1.00f);
	colors[ImGuiCol_Button]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_ButtonHovered]          = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_ButtonActive]           = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_Header]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_HeaderHovered]          = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_HeaderActive]           = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_Separator]              = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_SeparatorActive]        = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_ResizeGrip]             = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_InputTextCursor]        = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TabHovered]             = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_Tab]                    = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_TabSelected]            = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
	colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TabDimmed]              = ImVec4(0.22f, 0.22f, 0.22f, 0.78f);
	colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.42f, 0.42f, 0.42f, 0.78f);
	colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
	colors[ImGuiCol_DockingPreview]         = ImVec4(0.70f, 0.61f, 0.47f, 1.00f);
	colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_PlotLines]              = ImVec4(0.70f, 0.61f, 0.47f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.95f, 0.83f, 0.66f, 1.00f);
	colors[ImGuiCol_PlotHistogram]          = ImVec4(0.70f, 0.61f, 0.47f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.95f, 0.83f, 0.66f, 1.00f);
	colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
	colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
	colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
	colors[ImGuiCol_TextLink]               = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_TreeLines]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_DragDropTarget]         = ImVec4(0.95f, 0.83f, 0.66f, 1.00f);
	colors[ImGuiCol_NavCursor]              = ImVec4(0.95f, 0.83f, 0.66f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void ImGuiWrapMousePosition(const ImRect& inclusive_wrap_rect) {
	auto& io = ImGui::GetIO();
	auto* viewport = ImGui::GetWindowViewport();
	
	auto wrap_rect = ImRect(inclusive_wrap_rect.Min, inclusive_wrap_rect.Max - ImVec2(1.f, 1.f));
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

struct ImGuiMouseLock {
	ImGuiMouseButton locked_mouse_button = ImGuiMouseButton_COUNT;
	ImVec2 locked_mouse_pos;
	
	void Update(ImGuiMouseButton button, bool should_lock_mouse, const ImRect& inclusive_lock_rect) {
		if (should_lock_mouse && locked_mouse_button == ImGuiMouseButton_COUNT && ImGui::IsMouseClicked(button)) {
			locked_mouse_button = button;
			locked_mouse_pos = ImGui::GetMousePos();
		}
		
		if (locked_mouse_button == button && ImGui::IsMouseDown(button) == false) {
			locked_mouse_button = ImGuiMouseButton_COUNT;
			ImGui::TeleportMousePos(locked_mouse_pos);
		}
		
		if (locked_mouse_button == button) {
			ImGuiWrapMousePosition(inclusive_lock_rect);
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		}
	}
};

#define ImGuiScopeID(...) ImGui::PushID(__VA_ARGS__); defer{ ImGui::PopID(); }


template<typename T>
static GpuComponentUploadBuffer AllocateGpuComponentUploadBuffer(RecordContext* record_context, u32 count, ECS::GpuComponent<T> buffer) {
	auto [data_gpu_address,    data_cpu_address]    = AllocateTransientUploadBuffer<T,   16u>(record_context, count);
	auto [indices_gpu_address, indices_cpu_address] = AllocateTransientUploadBuffer<u32, 16u>(record_context, count);
	
	GpuComponentUploadBuffer result;
	result.count  = 0;
	result.stride = sizeof(T);
	result.data_cpu_address     = (u8*)data_cpu_address;
	result.indices_cpu_address  = indices_cpu_address;
	result.dst_data_gpu_address = buffer.resource_id;
	result.data_gpu_address     = data_gpu_address;
	result.indices_gpu_address  = indices_gpu_address;
	
	return result;
};

template<typename T>
static void QueueGpuUpload(GpuComponentUploadBuffer& view, u32 dst_index, const T& element) {
	u32 src_index = view.count++;
	((T*)view.data_cpu_address)[src_index] = element;
	view.indices_cpu_address[src_index]    = dst_index;
};

s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	extern void BasicExamples(StackAllocator* alloc);
	BasicExamples(&alloc);
	
	auto imgui_heap = CreateHeapAllocator(2 * 1024 * 1024);
	ImGui::SetAllocatorFunctions(
		[](u64   size,   void* heap) { return ((HeapAllocator*)heap)->Allocate(size);     },
		[](void* memory, void* heap) { return ((HeapAllocator*)heap)->Deallocate(memory); },
		&imgui_heap
	);
	
	ImGui_ImplWin32_EnableDpiAwareness();
	
	ImGui::CreateContext();
	defer{ ImGui::DestroyContext(); };
	
	Array<BasicVertex>  vertices;
	Array<BasicMeshlet> meshlets;
	Array<u8>           indices;
	{
		extern void ImportFbxMeshFile(StackAllocator* alloc, String filepath, Array<BasicVertex>& result_vertices, Array<BasicMeshlet>& result_meshlets, Array<u8>& result_indices);
		ImportFbxMeshFile(&alloc, "./Assets/Source/Torus/Torus.fbx"_sl, vertices, meshlets, indices);
	}
	
	auto& io = ImGui::GetIO();
	io.IniFilename = "./Build/ImGuiSettings.ini";
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	
	ImFontConfig font_config = {};
	font_config.GlyphOffset.y = -1.f;
	io.Fonts->Flags |= ImFontAtlasFlags_NoMouseCursors;
	io.Fonts->AddFontFromFileTTF("./Assets/OpenSans-Regular.ttf", 18.f, &font_config);
	
	auto* window = SystemCreateWindow(&alloc, "Engine"_sl);
	defer{ SystemReleaseWindow(window); };
	
	ImGui_ImplWin32_Init(window->hwnd);
	defer{ ImGui_ImplWin32_Shutdown(); };
	
	auto* graphics_context = CreateGraphicsContext(&alloc);
	defer{ ReleaseGraphicsContext(graphics_context, &alloc); };
	
	// TODO: Dynamically switch between HDR and SDR, add tone mappers for both.
	auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R16G16B16A16_FLOAT);
	// auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R8G8B8A8_UNORM_SRGB);
	defer{ ReleaseWindowSwapChain(swap_chain, graphics_context); };
	
	ImGuiSetCustomStyle();
	
	FixedCountArray<NativeBufferResource, number_of_frames_in_flight> upload_buffers;
	FixedCountArray<u8*, number_of_frames_in_flight> upload_buffer_cpu_addresses;
	compile_const u32 upload_buffer_size = 32 * 1024 * 1024;
	
	for (u32 i = 0; i < number_of_frames_in_flight; i += 1) {
		upload_buffers[i] = CreateBufferResource(graphics_context, upload_buffer_size, GpuMemoryAccessType::Upload, &upload_buffer_cpu_addresses[i]);
	}
	u32 upload_buffer_index = 0;
	
	VirtualResourceTable resource_table;
	ArrayReserve(resource_table.virtual_resources, &alloc, (u64)VirtualResourceID::Count + 16);
	ArrayResizeMemset(resource_table.virtual_resources, &alloc, (u64)VirtualResourceID::Count);
	
	EntitySystem entity_system;
	InitializeEntitySystem(entity_system);
	defer{ ReleaseHeapAllocator(entity_system.heap); };
	
	compile_const u32 mesh_grid_size = 16;
	compile_const u32 mesh_instance_count = mesh_grid_size * mesh_grid_size;
	
	auto scene_save_load_path = "./Assets/Scene.csb"_sl;
	{
		TempAllocationScope(&alloc);
		SaveLoadBuffer buffer;
		if (OpenSaveLoadBuffer(&alloc, scene_save_load_path, true, buffer)) {
			SaveLoadEntitySystem(buffer, entity_system);
			CloseSaveLoadBuffer(buffer);
		} else {
			WorldEntityType world_entity;
			{
				auto entity_result = CreateEntities<WorldEntityType>(&alloc, entity_system, 1);
				auto* entity_array = QueryEntityTypeArray<WorldEntityType>(entity_system);
				world_entity = ExtractComponentStreams<WorldEntityType>(entity_array, entity_result.stream_offset);
			}
			
			{
				auto entity_result = CreateEntities<CameraEntityType>(&alloc, entity_system, 1);
				auto* entity_array = QueryEntityTypeArray<CameraEntityType>(entity_system);
				auto camera_entity = ExtractComponentStreams<CameraEntityType>(entity_array, entity_result.stream_offset);
				
				camera_entity.rotation->rotation =
					Math::AxisAngleToQuat(float3(0.f, 0.f, 1.f), -90.f * Math::degrees_to_radians) *
					Math::AxisAngleToQuat(float3(1.f, 0.f, 0.f), -90.f * Math::degrees_to_radians);
				
				camera_entity.name->name = StringCopy(&entity_system.heap, "Camera"_sl);
				world_entity.camera_entity->guid = camera_entity.guid->guid;
			}
			
			{
				auto entity_result = CreateEntities<MeshEntityType>(&alloc, entity_system, mesh_instance_count);
				auto* entity_array = QueryEntityTypeArray<MeshEntityType>(entity_system);
				auto mesh_entities = ExtractComponentStreams<PositionQuery>(entity_array, entity_result.stream_offset);
				
				compile_const float spacing = 2.f;
				compile_const float center_offset = -(float)mesh_grid_size * 0.5f * spacing;
				for (u32 i = 0; i < mesh_instance_count; i += 1) {
					mesh_entities.position[i].position = float3((i % mesh_grid_size) * spacing + center_offset, (i / mesh_grid_size) * spacing + center_offset, 0.f);
				}
			}
		}
	}
	
	u64 world_entity_guid = ExtractComponentStreams<GuidQuery>(QueryEntityTypeArray<WorldEntityType>(entity_system), 0).guid->guid;
	
	HashTable<u64, void> selected_entities_hash_table;
	
	ImGuiMouseLock mouse_lock;
	
	u64 frame_allocation_size = 0;
	u64 transient_upload_allocation_size = 0;
	while (window->should_close == false) {
		TempAllocationScopeNamed(frame_initial_size, &alloc);
		
		SystemPollWindowEvents(window);
		
		ResizeWindowSwapChain(swap_chain, graphics_context, window->size);
		
		WindowSwapChainBeginFrame(swap_chain, graphics_context, &alloc);
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		
		// Must be scaled by DPI.
		compile_const float default_icon_size          = 9.f;
		compile_const float default_button_width       = 47.f;
		compile_const float default_title_bar_height   = 30.f;
		compile_const float maximized_title_bar_height = 23.f;
		
		float dpi_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window->hwnd);
		float title_bar_height = (window->state == WindowState::Maximized ? maximized_title_bar_height : default_title_bar_height) * dpi_scale;
		float button_width = default_button_width * dpi_scale;
		
		auto half_button_size = ImVec2(ceilf(button_width * 0.5f), floorf(title_bar_height * 0.5f));
		auto button_size      = half_button_size * 2.f;
		
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, floorf((button_size.y - ImGui::GetFontSize()) * 0.5f)));
		ImGui::BeginMainMenuBar();
		ImGui::PopStyleVar();
		
		window->titlebar_hovered = ImGui::IsWindowHovered() && (ImGui::IsAnyItemHovered() == false);
		
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - button_size.x * 3.f, 0.f));
		
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, half_button_size.y);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		auto* draw_list = ImGui::GetWindowDrawList();
		
		float icon_size = (default_icon_size * dpi_scale);
		float half_icon_size = floorf(icon_size * 0.5f) + 0.5f + (1.f / 128.f);
		float icon_line_thickness = floorf(dpi_scale);
		
		auto button_center = ImGui::GetCursorScreenPos() + half_button_size;
		if (ImGui::Button("##Minimize", button_size)) {
			window->requested_state = WindowState::Minimized;
		}
		
		{
			draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, 0.5f));
			draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, 0.5f));
			draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, icon_line_thickness);
		}
		
		
		button_center = ImGui::GetCursorScreenPos() + half_button_size;
		if (ImGui::Button("##MaximizeOrRestore", button_size)) {
			window->requested_state = window->state == WindowState::Maximized ? WindowState::Floating : WindowState::Maximized;
		}
		
		{
			float offset = 0.f;
			if (window->state == WindowState::Maximized) {
				offset = floorf(icon_size * 0.25f);
				draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, +half_icon_size) + ImVec2(-offset, -offset));
				draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, +half_icon_size) + ImVec2(0.f, -offset));
				draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, -half_icon_size));
				draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, -half_icon_size) + ImVec2(+offset, 0.f));
				draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, -half_icon_size) + ImVec2(+offset, +offset));
				draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, icon_line_thickness);
			}
			
			draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, +half_icon_size) + ImVec2(-offset, 0.f));
			draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, +half_icon_size));
			draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, -half_icon_size) + ImVec2(0.f, +offset));
			draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, -half_icon_size) + ImVec2(-offset, +offset));
			draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_Closed, icon_line_thickness);
		}
		
		button_center = ImGui::GetCursorScreenPos() + half_button_size;
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF1C2BC4);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  0xFF3040C8);
		if (ImGui::Button("##Close", button_size)) {
			window->should_close = true;
		}
		ImGui::PopStyleColor(2);
		
		{
			draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, +half_icon_size));
			draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, -half_icon_size));
			draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, icon_line_thickness);
			
			draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, +half_icon_size));
			draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, -half_icon_size));
			draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, icon_line_thickness);
		}
		
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
		ImGui::EndMainMenuBar();
		ImGui::PopStyleVar();
		
		ImGui::ShowDemoWindow(nullptr);
		ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar);
		
		RecordContext record_context;
		record_context.alloc   = &alloc;
		record_context.context = graphics_context;
		record_context.resource_table = &resource_table;
		
		resource_table.virtual_resources.count = (u64)VirtualResourceID::Count;
		resource_table.Set(VirtualResourceID::CurrentBackBuffer, WindowSwapGetCurrentBackBuffer(swap_chain), swap_chain->size);
		resource_table.Set(VirtualResourceID::TransientUploadBuffer, upload_buffers[upload_buffer_index], upload_buffer_size, upload_buffer_cpu_addresses[upload_buffer_index]);
		upload_buffer_index = (upload_buffer_index + 1) % number_of_frames_in_flight;
		
		struct ImGuiDescriptorTable : HLSL::BaseDescriptorTable {
			HLSL::Texture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
		};
		HLSL::DescriptorTable<ImGuiDescriptorTable> root_descriptor_table = { 0, 1 };
		
		auto& descriptor_table = AllocateDescriptorTable(&record_context, root_descriptor_table);
		
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.f, 0.f));
		ImGui::Begin("Scene");
		
		auto window_size = float2(ImGui::GetContentRegionAvail());
		auto window_pos  = ImGui::GetWindowPos();
		ImGui::ImageButton("Scene", descriptor_table.descriptor_heap_offset, ImVec2(window_size.x, window_size.y));
		bool scene_hovered = ImGui::IsItemHovered();
		bool scene_focused = ImGui::IsItemFocused();
		
		ImRect mouse_lock_rect;
		mouse_lock_rect.Min = window_pos;
		mouse_lock_rect.Max = window_pos + ImGui::GetWindowSize();
		mouse_lock.Update(ImGuiMouseButton_Left,  scene_hovered, mouse_lock_rect);
		mouse_lock.Update(ImGuiMouseButton_Right, scene_hovered, mouse_lock_rect);
		
		ImGui::End();
		ImGui::PopStyleVar(2);
		
		ImGui::Begin("Stats");
		
		ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused);
		bool should_save_scene = ImGui::Button("Save State"); ImGui::SameLine();
		bool should_load_scene = ImGui::Button("Load State") && (should_save_scene == false);
		
		if (should_save_scene || should_load_scene) {
			TempAllocationScope(&alloc);
			SaveLoadBuffer buffer;
			if (OpenSaveLoadBuffer(&alloc, scene_save_load_path, should_load_scene, buffer)) {
				SaveLoadEntitySystem(buffer, entity_system);
				CloseSaveLoadBuffer(buffer);
			}
		}
		
		auto world_entity = QueryEntityByGUID<WorldEntityType>(entity_system, world_entity_guid);
		auto camera_entity = QueryEntityByGUID<CameraEntityType>(entity_system, world_entity.camera_entity->guid);
		auto& camera_transform_type       = camera_entity.camera->transform_type;
		auto& vertical_fov_degrees        = camera_entity.camera->vertical_fov_degrees;
		auto& near_depth                  = camera_entity.camera->near_depth;
		auto& view_to_world_quat          = camera_entity.rotation->rotation;
		auto& world_space_camera_position = camera_entity.position->position;
		
		ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
		ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
		ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
		ImGui::Text("ImGui Alloc Size: %llu", imgui_heap.ComputeTotalMemoryUsage());
		ImGui::Combo("Camera Transform Type", (s32*)&camera_transform_type, "Perspective\0Orthographic\0");
		ImGui::SliderFloat("Vertical Field Of View", &vertical_fov_degrees, 10.f, 135.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Camera Near Depth", &near_depth, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Meshlet Target Error Pixels", &world_entity.renderer_world->meshlet_target_error_pixels, 1.f, 128.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Sun Elevation", &world_entity.renderer_world->sun_elevation_degrees, -10.f, +190.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::Text("World Space Camera Position: (%.3f, %.3f, %.3f)", world_space_camera_position.x, world_space_camera_position.y, world_space_camera_position.z);
		
		ImGui::BeginGroup();
		if (ImGui::CollapsingHeader("GUID/Name Query")) {
			u32 entity_count = 0;
			
			auto entity_view = QueryEntities<GuidNameQuery>(&alloc, entity_system);
			for (auto* entity_array : entity_view) {
				entity_count += entity_array->count;
			}
			
			auto apply_requests = [&](ImGuiMultiSelectIO* ms_io) {
				for (auto& request : ms_io->Requests) {
					if (request.Type == ImGuiSelectionRequestType_SetAll) {
						if (request.Selected) {
							for (auto* entity_array : entity_view) {
								auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
								for (u32 index = 0; index < entity_array->count; index += 1) {
									auto& [guid] = streams.guid[index];
									HashTableAddOrFind(selected_entities_hash_table, &imgui_heap, guid);
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
							for (u32 index = start_index; index < end_index; index += 1) {
								auto& [guid] = streams.guid[index - global_index];
								if (request.Selected) {
									HashTableAddOrFind(selected_entities_hash_table, &imgui_heap, guid);
								} else {
									HashTableRemove(selected_entities_hash_table, guid);
								}
							}
						}
					}
				}
			};
			
			auto* ms_io = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid, (s32)selected_entities_hash_table.count, (s32)entity_count);
			apply_requests(ms_io);
			
			ImGuiListClipper clipper;
			clipper.Begin(entity_count);
			if (ms_io->RangeSrcItem != -1) clipper.IncludeItemByIndex((s32)ms_io->RangeSrcItem);
			
			while (clipper.Step()) {
				u32 global_index = 0;
				for (auto* entity_array : entity_view) {
					u32 start_index = Max(clipper.DisplayStart, global_index);
					u32 end_index   = Min(clipper.DisplayEnd,   global_index + entity_array->count);
					defer{ global_index += entity_array->count; };
					
					if (start_index >= end_index) continue;
					
					auto streams = ExtractComponentStreams<GuidNameQuery>(entity_array);
					for (u32 index = start_index; index < end_index; index += 1) {
						auto& [guid] = streams.guid[index - global_index];
						auto& [name] = streams.name[index - global_index];
						
						ImGuiScopeID((void*)guid);
						
						ImGui::Bullet();
						ImGui::SameLine();
						ImGui::Text("%u", entity_array->entity_type_id.index);
						ImGui::SameLine();
						ImGui::Text("0x%llX", guid);
						ImGui::SameLine();
						
						bool is_selected = HashTableFind(selected_entities_hash_table, guid) != nullptr;
						
						ImGui::SetNextItemSelectionUserData(index);
						ImGui::Selectable(name.count ? name.data : "EmptyName", is_selected);
					}
				}
			}
			
			ms_io = ImGui::EndMultiSelect();
			apply_requests(ms_io);
			
			if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
				for (auto& [guid] : selected_entities_hash_table) {
					RemoveEntityByGUID(entity_system, guid);
				}
				HashTableClear(selected_entities_hash_table);
			}
		}
		ImGui::EndGroup();
		
		ImGui::End();
		
		if (scene_focused) {
			float base_speed = 10.f; // m/s
			float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
			
			auto  world_to_view = Math::QuatToRotationMatrix(Math::Conjugate(view_to_world_quat));
			float move_distance = base_speed * sensetivity_scale * io.DeltaTime;
			if (ImGui::IsKeyDown(ImGuiKey_D, ImGuiKeyOwner_NoOwner)) world_space_camera_position += world_to_view.r0 * +move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_A, ImGuiKeyOwner_NoOwner)) world_space_camera_position += world_to_view.r0 * -move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_W, ImGuiKeyOwner_NoOwner)) world_space_camera_position += world_to_view.r2 * +move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_S, ImGuiKeyOwner_NoOwner)) world_space_camera_position += world_to_view.r2 * -move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_Q, ImGuiKeyOwner_NoOwner)) world_space_camera_position += world_to_view.r1 * +move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_E, ImGuiKeyOwner_NoOwner)) world_space_camera_position += world_to_view.r1 * -move_distance;
		}
		
		if (scene_hovered && io.MouseWheel != 0.f && mouse_lock.locked_mouse_button == ImGuiMouseButton_COUNT) {
			float4 view_to_clip_coef;
			if (camera_transform_type == CameraTransformType::Perspective) {
				view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
			} else {
				view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
			}
			auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
			
			auto uv = float2(ImGui::GetMousePos() - window_pos) / float2(window_size);
			auto ray_info = Math::RayInfoFromScreenUv(uv, clip_to_view_coef);
			
			auto view_to_world = Math::QuatToRotationMatrix(view_to_world_quat);
			
			float meters_per_click = 1.f;
			float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
			
			float move_distance = io.MouseWheel * meters_per_click * sensetivity_scale;
			world_space_camera_position += (view_to_world * ray_info.direction) * move_distance;
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
			world_space_camera_position += world_to_view.r0 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.x);
			world_space_camera_position += world_to_view.r1 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.y);
		}
		
		if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_F4)) {
			window->should_close = true;
		}
		
		
		Array<GpuComponentUploadBuffer> gpu_uploads;
		for (auto* entity_array : QueryEntities<PositionRotationScaleGpuTransformQuery>(&alloc, entity_system)) {
			u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
			if (dirty_entity_count == 0) continue;
			
			auto streams = ExtractComponentStreams<PositionRotationScaleGpuTransformQuery>(entity_array);
			
			auto gpu_transform = AllocateGpuComponentUploadBuffer(&record_context, dirty_entity_count, streams.gpu_transform);
			
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				GpuTransform transform;
				transform.position = streams.position[i].position;
				transform.scale    = streams.scale[i].scale;
				transform.rotation = streams.rotation[i].rotation;
				QueueGpuUpload(gpu_transform, (u32)i, transform);
			}
			ArrayAppend(gpu_uploads, &alloc, gpu_transform);
		}
		
		auto renderer_world = world_entity.renderer_world;
		renderer_world->window_size = window_size;
		renderer_world->gpu_uploads = gpu_uploads;
		renderer_world->vertices = vertices;
		renderer_world->meshlets = meshlets;
		renderer_world->indices  = indices;
		
		BuildRenderPassesForFrame(&record_context, &entity_system, world_entity_guid);
		
		WindowSwapChainEndFrame(swap_chain, graphics_context, &alloc, record_context);
		
		ClearEntityDirtyMasks(entity_system);
		
		frame_allocation_size = (alloc.total_allocated_size - frame_initial_size);
		transient_upload_allocation_size = record_context.upload_buffer_offset;
	}
	WaitForLastFrame(graphics_context);
	
	for (auto& resource : resource_table.virtual_resources) {
		if (resource.type == VirtualResource::Type::VirtualBuffer) {
			ReleaseBufferResource(graphics_context, resource.buffer.resource);
		} else if (resource.type == VirtualResource::Type::VirtualTexture) {
			ReleaseTextureResource(graphics_context, resource.texture.resource);
		}
	}
	
	ReleaseBufferResource(graphics_context, resource_table.virtual_resources[(u32)VirtualResourceID::MeshEntityGpuTransform].buffer.resource);
	
	for (auto& buffer : upload_buffers) {
		ReleaseBufferResource(graphics_context, buffer);
	}
	
	for (auto* texture : ImGui::GetPlatformIO().Textures) {
		ReleaseTextureResource(graphics_context, { texture->BackendUserData });
	}
	
	return 0;
}
