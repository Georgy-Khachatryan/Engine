#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicHashTable.h"
#include "SystemWindow.h"
#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/backends/imgui_impl_win32.h>

static void BasicExamples(StackAllocator* alloc) {
	// Stack allocator:
	{
		TempAllocationScopeNamed(initial_size, alloc);
		
		void* memory0 = alloc->Allocate(32, 32);
		void* memory1 = alloc->Reallocate(memory0, 32, 128, 32);
		DebugAssert(memory0 == memory1, "Reallocation failed.");
		
		void* memory2 = alloc->Allocate(64);
		void* memory3 = alloc->Reallocate(memory0, 128, 1024, 32);
		DebugAssert(memory0 != memory3, "Reallocation failed.");
		
		alloc->Deallocate(memory3, 1024);
		alloc->Deallocate(memory2, 64);
		alloc->Deallocate(memory1, 128);
		DebugAssert(alloc->total_allocated_size == initial_size, "Deallocation failed.");
	}
	
	// String formatting:
	{
		TempAllocationScope(alloc);
		
		auto alloc_address_string = StringFormat(alloc, "Number: %u.", 10u);
		DebugAssert(alloc_address_string == "Number: 10."_sl, "String is incorrectly formatted.");
	}
	
	// String utf8 <-> utf16 conversion:
	{	
		TempAllocationScope(alloc);
		
		auto utf8_source     = u8"Orange կատու."_sl;
		auto utf16_converted = StringUtf8ToUtf16(alloc, utf8_source);
		auto utf8_converted  = StringUtf16ToUtf8(alloc, utf16_converted);
		
		DebugAssert(utf8_source == utf8_converted, "String mismatch after utf8 -> utf16 -> utf8 conversion.");
	}
	
	// Resizable array:
	{
		TempAllocationScope(alloc);
		
		Array<u32> values;
		ArrayReserve(values, alloc, 10);
		
		for (u32 i = 0; i < 10; i += 1) {
			ArrayAppend(values, i);
		}
		
		for (u32 i = 10; i < 20; i += 1) {
			ArrayAppend(values, alloc, i);
		}
		
		u32 first = ArrayPopFirst(values);
		DebugAssert(first == 0, "Incorrect first value.");
		
		u32 last = ArrayPopLast(values);
		DebugAssert(last == 19, "Incorrect last value.");
		
		ArrayEraseSwapLast(values, 2);
		DebugAssert(values[2] == 18, "Erase swap last is incorrect.");
		
		ArrayEmplace(values) = 100;
		
		ArrayErase(values, 1);
		DebugAssert(values[1] == 18, "Erase is incorrect.");
	}
	
	// Fixed capacity array:
	{
		FixedCapacityArray<u32, 10> values;
		
		for (u32 i = 0; i < 10; i += 1) {
			ArrayAppend(values, i);
		}
		
		u32 first = ArrayPopFirst(values);
		DebugAssert(first == 0, "Incorrect first value.");
		
		u32 last = ArrayPopLast(values);
		DebugAssert(last == 9, "Incorrect last value.");
		
		ArrayEraseSwapLast(values, 2);
		DebugAssert(values[2] == 8, "Erase swap last is incorrect.");
		
		ArrayEmplace(values) = 100;
		
		ArrayErase(values, 1);
		DebugAssert(values[1] == 8, "Erase is incorrect.");
	}
	
	// Fixed count array:
	{
		FixedCountArray<u32, 10> values;
		
		for (u32 i = 0; i < 10; i += 1) {
			values[i] = i;
		}
		
		u32 first = ArrayFirstElement(values);
		DebugAssert(first == 0, "Incorrect first value.");
		
		u32 last = ArrayLastElement(values);
		DebugAssert(last == 9, "Incorrect last value.");
	}
	
	{
		auto heap = CreateHeapAllocator(64 * 1024);
		defer{ ReleaseHeapAllocator(heap); };
		
		{
			auto* memory0 = heap.Allocate(32 * 1024 - 8);
			auto* memory1 = heap.Allocate(30720 - 8);
			
			heap.Deallocate(memory0);
			heap.Deallocate(memory1);
		}
		
		{
			auto* memory0 = heap.Allocate(128 * 1024 - 16);
			heap.Deallocate(memory0);
		}
		
		{
			auto* memory0 = heap.Allocate(1024);
			auto* memory1 = heap.Allocate(2048);
			auto* memory2 = heap.Allocate(4096);
			auto* memory3 = heap.Allocate(8192);
			auto* memory4 = heap.Allocate(16384);
			
			heap.Deallocate(memory1);
			heap.Deallocate(memory3);
			heap.Deallocate(memory2);
			heap.Deallocate(memory0);
			heap.Deallocate(memory4);
		}
		
		{
			auto* memory0 = heap.Allocate(1024);
			auto* memory1 = heap.Allocate(1024);
			auto* memory2 = heap.Allocate(1024);
			auto* memory3 = heap.Allocate(1024);
			auto* memory4 = heap.Allocate(1024);
			
			heap.Deallocate(memory1);
			heap.Deallocate(memory3);
			heap.Deallocate(memory2);
			heap.Deallocate(memory0);
			heap.Deallocate(memory4);
		}
		
		{
			auto* memory0 = heap.Allocate(1024);
			auto* memory1 = heap.Allocate(1024);
			auto* memory2 = heap.Allocate(1024);
			auto* memory3 = heap.Allocate(1024);
			auto* memory4 = heap.Allocate(1024);
			
			heap.Deallocate(memory1);
			heap.Deallocate(memory3);
			memory2 = heap.Reallocate(memory2, 1024, 1024 * 3);
			DebugAssert(memory2 == memory1, "Reallocation failed.");
			heap.Deallocate(memory0);
			heap.Deallocate(memory4);
			heap.Deallocate(memory2);
		}
		
		{
			Array<u32> values;
			ArrayReserve(values, &heap, 10);
			defer{ heap.Deallocate(values.data); };
			
			for (u32 i = 0; i < 10; i += 1) {
				ArrayAppend(values, i);
			}
			
			for (u32 i = 10; i < 20; i += 1) {
				ArrayAppend(values, &heap, i);
			}
			
			u32 first = ArrayPopFirst(values);
			DebugAssert(first == 0, "Incorrect first value.");
			
			u32 last = ArrayPopLast(values);
			DebugAssert(last == 19, "Incorrect last value.");
			
			ArrayEraseSwapLast(values, 2);
			DebugAssert(values[2] == 18, "Erase swap last is incorrect.");
			
			ArrayEmplace(values) = 100;
			
			ArrayErase(values, 1);
			DebugAssert(values[1] == 18, "Erase is incorrect.");
		}
	}
	
	{
		auto heap = CreateHeapAllocator(64 * 1024);
		defer{ ReleaseHeapAllocator(heap); };
		
		{
			HashTable<u64, u64> hash_table;
			HashTableReserve(hash_table, &heap, 128);
			defer{ HashTableDeallocate(hash_table, &heap); };
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, i, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, i)->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, i, i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, i)->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, i);
			}
		}
		
		{
			HashTable<String, u64> hash_table;
			HashTableReserve(hash_table, &heap, 128);
			defer{ HashTableDeallocate(hash_table, &heap); };
			
			TempAllocationScope(alloc);
			
			Array<String> keys;
			ArrayResize(keys, alloc, 128);
			
			for (u64 i = 0; i < 128; i += 1) {
				keys[i] = StringFormat(alloc, "Key: %llu", i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, keys[i], i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, keys[i])->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, keys[i]);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableAddOrFind(hash_table, &heap, keys[i], i);
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				u64 value = HashTableFind(hash_table, keys[i])->value;
				DebugAssert(value == i, "Failed to find an item");
			}
			
			for (u64 i = 0; i < 128; i += 1) {
				HashTableRemove(hash_table, keys[i]);
			}
		}
	}
}

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

s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
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
	
	Array<BasicVertex> vertices;
	Array<BasicMeshlet> meshlets;
	Array<u8> indices;
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
	defer{ ReleaseGraphicsContext(graphics_context); };
	
	// TODO: Dynamically switch between HDR and SDR, add tone mappers for both.
	auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R16G16B16A16_FLOAT);
	// auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R8G8B8A8_UNORM_SRGB);
	defer{ ReleaseWindowSwapChain(swap_chain, graphics_context); };
	
	ImGuiSetCustomStyle();
	
	FixedCountArray<NativeBufferResource, number_of_frames_in_flight> upload_buffers;
	FixedCountArray<u8*, number_of_frames_in_flight> upload_buffer_cpu_addresses;
	compile_const u32 imgui_upload_buffer_size = 8 * 1024 * 1024;
	
	for (u32 i = 0; i < number_of_frames_in_flight; i += 1) {
		upload_buffers[i] = CreateBufferResource(graphics_context, imgui_upload_buffer_size, &upload_buffer_cpu_addresses[i]);
	}
	u32 upload_buffer_index = 0;
	
	VirtualResourceTable resource_table;
	ArrayReserve(resource_table.virtual_resources, &alloc, (u64)VirtualResourceID::Count + 16);
	ArrayResizeMemset(resource_table.virtual_resources, &alloc, (u64)VirtualResourceID::Count);
	{
		using ResourceID = VirtualResourceID;
		
		auto& table = resource_table;
		table.Set(ResourceID::TransmittanceLut,      TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::transmittance_lut_size));
		table.Set(ResourceID::MultipleScatteringLut, TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::multiple_scattering_lut_size));
		table.Set(ResourceID::SkyPanoramaLut,        TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::sky_panorama_lut_size));
	}
	
	float vertical_fov_degrees  = 75.f;
	float sun_elevation_degrees = 3.f;
	
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
		resource_table.Set(VirtualResourceID::TransientUploadBuffer, upload_buffers[upload_buffer_index], imgui_upload_buffer_size, upload_buffer_cpu_addresses[upload_buffer_index]);
		upload_buffer_index = (upload_buffer_index + 1) % number_of_frames_in_flight;
		
		struct ImGuiDescriptorTable : HLSL::BaseDescriptorTable {
			HLSL::Texture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
		};
		HLSL::DescriptorTable<ImGuiDescriptorTable> root_descriptor_table = { 0, 1 };
		
		auto& descriptor_table = AllocateDescriptorTable(&record_context, root_descriptor_table);
		
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::Begin("Scene");
		auto window_size = float2(ImGui::GetContentRegionAvail());
		ImGui::Image(descriptor_table.descriptor_heap_offset, ImVec2(window_size.x, window_size.y));
		ImGui::End();
		ImGui::PopStyleVar();
		
		// Clamp render target size to a reasonable minimum. Aspect ratio for view to clip is still computed using unclamped values.
		uint2 render_target_size = uint2((u32)Max(window_size.x, 16.f), (u32)Max(window_size.y, 16.f));
		resource_table.Set(VirtualResourceID::SceneRadiance, TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size));
		resource_table.Set(VirtualResourceID::DepthStencil,  TextureSize(TextureFormat::D32_FLOAT, render_target_size));
		
		compile_const float near_depth = 0.1f;
		
		SceneConstants scene;
		scene.render_target_size     = float2(render_target_size);
		scene.inv_render_target_size = float2(1.f) / scene.render_target_size;
#if 1
		scene.view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
		scene.clip_to_view_coef = Math::ViewToClipInverse(scene.view_to_clip_coef);
#else
		scene.view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
		scene.clip_to_view_coef = Math::ViewToClipInverse(scene.view_to_clip_coef);
#endif
		scene.view_to_world[0] = float4(0.f,  0.f, 1.f, 0.f);
		scene.view_to_world[1] = float4(-1.f, 0.f, 0.f, 0.f);
		scene.view_to_world[2] = float4(0.f, -1.f, 0.f, 0.f);
		scene.world_to_view[0] = float4(0.f, -1.f, 0.f, 0.f);
		scene.world_to_view[1] = float4(0.f, 0.f, -1.f, 0.f);
		scene.world_to_view[2] = float4(1.f, 0.f,  0.f, 0.f);
		
		AtmosphereParameters atmosphere_parameters;
		
		ImGui::Begin("Stats");
		ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
		ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
		ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
		ImGui::Text("ImGui Alloc Size: %llu", imgui_heap.ComputeTotalMemoryUsage());
		ImGui::SliderFloat("Vertical Field Of View", &vertical_fov_degrees, 10.f, 135.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		
		ImGui::SliderFloat("Sun Elevation", &sun_elevation_degrees, -10.f, +190.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		atmosphere_parameters.world_space_sun_direction.x = cosf(sun_elevation_degrees * Math::degrees_to_radians);
		atmosphere_parameters.world_space_sun_direction.z = sinf(sun_elevation_degrees * Math::degrees_to_radians);
		
		ImGui::End();
		
		if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_F4)) {
			window->should_close = true;
		}
		
		auto [scene_constants_gpu_address, scene_constants_cpu_address] = AllocateTransientUploadBuffer<SceneConstants>(&record_context);
		auto [atmosphere_parameters_gpu_address, atmosphere_parameters_cpu_address] = AllocateTransientUploadBuffer<AtmosphereParameters>(&record_context);
		*scene_constants_cpu_address = scene;
		*atmosphere_parameters_cpu_address = atmosphere_parameters;
		
		TransmittanceLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(&record_context);
		MultipleScatteringLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(&record_context);
		SkyPanoramaLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(&record_context);
		AtmosphereCompositeRenderPass{ atmosphere_parameters_gpu_address, scene_constants_gpu_address }.RecordPass(&record_context);
		
		{
			auto [vb_gpu_address, vb_cpu_address] = AllocateTransientUploadBuffer<BasicVertex,  sizeof(BasicVertex)>(&record_context,  (u32)vertices.count);
			auto [mb_gpu_address, mb_cpu_address] = AllocateTransientUploadBuffer<BasicMeshlet, sizeof(BasicMeshlet)>(&record_context, (u32)meshlets.count);
			auto [ib_gpu_address, ib_cpu_address] = AllocateTransientUploadBuffer<u8, 16>(&record_context, (u32)indices.count);
			memcpy(vb_cpu_address, vertices.data, vertices.count * sizeof(BasicVertex));
			memcpy(mb_cpu_address, meshlets.data, meshlets.count * sizeof(BasicMeshlet));
			memcpy(ib_cpu_address, indices.data,  indices.count  * sizeof(u8));
			
			BasicMeshRenderPass{ scene_constants_gpu_address, vb_gpu_address, mb_gpu_address, ib_gpu_address, (u32)vertices.count, (u32)meshlets.count, (u32)indices.count }.RecordPass(&record_context);
		}
		
		ImGuiRenderPass{}.RecordPass(&record_context);
		
		WindowSwapChainEndFrame(swap_chain, graphics_context, &alloc, record_context);
		
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
	
	for (auto& buffer : upload_buffers) {
		ReleaseBufferResource(graphics_context, buffer);
	}
	
	for (auto* texture : ImGui::GetPlatformIO().Textures) {
		ReleaseTextureResource(graphics_context, { texture->BackendUserData });
	}
	
	return 0;
}
