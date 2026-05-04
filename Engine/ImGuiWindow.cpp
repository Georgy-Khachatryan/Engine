#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "GraphicsApi/GraphicsApi.h"
#include "SystemWindow.h"
#include "ImGuiCustomWidgets.h"

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

void ImGuiInitializeContext(HeapAllocator* heap) {
	ImGui::SetAllocatorFunctions(
		[](u64   size,   void* heap) { return ((HeapAllocator*)heap)->Allocate(size);     },
		[](void* memory, void* heap) { return ((HeapAllocator*)heap)->Deallocate(memory); },
		heap
	);
	
	ImGui_ImplWin32_EnableDpiAwareness();
	ImGui::CreateContext();
	ImGui::CreateContext3D();
	
	
	auto& io = ImGui::GetIO();
	io.IniFilename = "./Build/ImGuiSettings.ini";
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	
	ImFontConfig font_config = {};
	font_config.GlyphOffset.y = -1.f;
	io.Fonts->Flags |= ImFontAtlasFlags_NoMouseCursors;
	io.Fonts->AddFontFromFileTTF("./Assets/OpenSans-Regular.ttf", 18.f, &font_config);
	
	ImGuiSetCustomStyle();
}

void ImGuiInitializeWindow(SystemWindow* window) {
	ProfilerScope("ImGuiInitializeWindow");
	ImGui_ImplWin32_Init(window->hwnd);
}

void ImGuiReleaseContext(GraphicsContext* graphics_context) {
	for (auto* texture : ImGui::GetPlatformIO().Textures) {
		ReleaseTextureResource(graphics_context, { texture->BackendUserData });
	}
	
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext3D();
	ImGui::DestroyContext();
	
	// ImGui heap is deallocated before the globals are deallocated. Set dummy allocator callbacks to make sure we don't crash.
	ImGui::SetAllocatorFunctions(
		[](u64   size,   void* heap) { return (void*)nullptr; },
		[](void* memory, void* heap) { return;         },
		nullptr
	);
}

static void ImGuiMainWindowMenuBar(SystemWindow* window) {
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
	
	
	float icon_size = (default_icon_size * dpi_scale);
	float half_icon_size = floorf(icon_size * 0.5f) + 0.5f + (1.f / 128.f);
	float icon_line_thickness = floorf(dpi_scale);
	
	auto* draw_list = ImGui::GetWindowDrawList();
	
	// Minimize Button:
	{
		auto button_center = ImGui::GetCursorScreenPos() + half_button_size;
		if (ImGui::Button("##Minimize", button_size)) {
			window->requested_state = WindowState::Minimized;
		}
		
		draw_list->PathLineTo(button_center + ImVec2(+half_icon_size, 0.5f));
		draw_list->PathLineTo(button_center + ImVec2(-half_icon_size, 0.5f));
		draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, icon_line_thickness);
	}
	
	// Maximize or Restore Button:
	{
		auto button_center = ImGui::GetCursorScreenPos() + half_button_size;
		if (ImGui::Button("##MaximizeOrRestore", button_size)) {
			window->requested_state = window->state == WindowState::Maximized ? WindowState::Floating : WindowState::Maximized;
		}
		
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
	
	// Close Button:
	{
		auto button_center = ImGui::GetCursorScreenPos() + half_button_size;
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF1C2BC4);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  0xFF3040C8);
		if (ImGui::Button("##Close", button_size)) {
			window->should_close = true;
		}
		ImGui::PopStyleColor(2);
		
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
}

void ImGuiBeginFrame(SystemWindow* window) {
	ProfilerScope("ImGuiBeginFrame");
	
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	
	if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_F4)) {
		window->should_close = true;
	}
	
	ImGuiMainWindowMenuBar(window);
	
	ImGui::ShowDemoWindow(nullptr);
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar);
}
