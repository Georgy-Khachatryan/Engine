#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "SystemWindow.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/backends/imgui_impl_win32.h>


s32 main() {
	auto alloc = CreateStackAllocator(256 * 1024, 64 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	

	ImGui_ImplWin32_EnableDpiAwareness();
	
	auto* window = SystemCreateWindow(&alloc, L"Engine");
	defer{ SystemReleaseWindow(window); };
	
	ImGui::CreateContext();
	defer{ ImGui::DestroyContext(); };
	
	auto& io = ImGui::GetIO();
	io.ConfigFlags  |= ImGuiConfigFlags_DockingEnable;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
	
	ImGui::StyleColorsDark();
	
	ImGui_ImplWin32_Init(window->hwnd);
	defer{ ImGui_ImplWin32_Shutdown(); };
	
	while (window->should_close == false) {
		SystemPollWindowEvents(window);
		
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		if (ImGui::IsKeyChordPressed(ImGuiKey_ModAlt | ImGuiKey_F4)) {
			window->should_close = true;
		}
		
		ImGui::Render();
	}
	
	return 0;
}
