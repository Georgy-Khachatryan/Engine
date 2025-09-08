#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "SystemWindow.h"
#include "GraphicsApi/GraphicsApi.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/backends/imgui_impl_win32.h>


s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	
	ImGui_ImplWin32_EnableDpiAwareness();
	
	ImGui::CreateContext();
	defer{ ImGui::DestroyContext(); };
	
	ImFontConfig font_config = {};
	font_config.GlyphOffset.y = -1.f;
	
	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.Fonts->AddFontFromFileTTF("./Assets/OpenSans-Regular.ttf", 18.f, &font_config);
	
	auto* window = SystemCreateWindow(&alloc, L"Engine");
	defer{ SystemReleaseWindow(window); };
	
	ImGui_ImplWin32_Init(window->hwnd);
	defer{ ImGui_ImplWin32_Shutdown(); };
	
	auto* graphics_context = CreateGraphicsContext(&alloc);
	defer{ ReleaseGraphicsContext(graphics_context); };
	
	auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd);
	defer{ ReleaseWindowSwapChain(swap_chain, graphics_context); };
	
	ImGui::StyleColorsDark();
	
	while (window->should_close == false) {
		SystemPollWindowEvents(window);
		
		ResizeWindowSwapChain(swap_chain, graphics_context, window->width, window->height);
		
		WindowSwapChainBeginFrame(swap_chain, graphics_context);
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		ImGui::ShowDemoWindow(nullptr);
		
		if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_F4)) {
			window->should_close = true;
		}
		
		ImGui::Render();
		
		WindowSwapChainEndFrame(swap_chain, graphics_context);
	}
	
	return 0;
}
