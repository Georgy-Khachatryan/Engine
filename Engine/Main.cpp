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
	
	
	auto& io = ImGui::GetIO();
	io.IniFilename = "./Build/ImGuiSettings.ini";
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	
	ImFontConfig font_config = {};
	font_config.GlyphOffset.y = -1.f;
	io.Fonts->Flags |= ImFontAtlasFlags_NoMouseCursors;
	io.Fonts->AddFontFromFileTTF("./Assets/OpenSans-Regular.ttf", 18.f, &font_config);
	
	auto* window = SystemCreateWindow(&alloc, L"Engine");
	defer{ SystemReleaseWindow(window); };
	
	ImGui_ImplWin32_Init(window->hwnd);
	defer{ ImGui_ImplWin32_Shutdown(); };
	
	auto* graphics_context = CreateGraphicsContext(&alloc);
	defer{ ReleaseGraphicsContext(graphics_context); };
	
	// auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R16G16B16A16_FLOAT);
	auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R8G8B8A8_UNORM_SRGB);
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
	
	u64 frame_allocation_size = 0;
	while (window->should_close == false) {
		TempAllocationScopeNamed(frame_initial_size, &alloc);
		
		SystemPollWindowEvents(window);
		
		ResizeWindowSwapChain(swap_chain, graphics_context, window->size);
		
		WindowSwapChainBeginFrame(swap_chain, graphics_context, &alloc);
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		resource_table.virtual_resources.count = (u64)VirtualResourceID::Count;
		resource_table.Set(VirtualResourceID::CurrentBackBuffer, WindowSwapGetCurrentBackBuffer(swap_chain), swap_chain->size);
		resource_table.Set(VirtualResourceID::TransientUploadBuffer, upload_buffers[upload_buffer_index], imgui_upload_buffer_size, upload_buffer_cpu_addresses[upload_buffer_index]);
		upload_buffer_index = (upload_buffer_index + 1) % number_of_frames_in_flight;
		
		RecordContext record_context;
		record_context.alloc   = &alloc;
		record_context.context = graphics_context;
		record_context.resource_table = &resource_table;
		
		ImGui::ShowDemoWindow(nullptr);
		
		ImGui::Begin("Stats");
		ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
		ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
		ImGui::PushFont(nullptr, 36.f);
		ImGui::Text("ImGui Alloc Size: %llu", imgui_heap.ComputeTotalMemoryUsage());
		ImGui::PopFont();
		ImGui::End();
		
		struct ImGuiDescriptorTable : HLSL::BaseDescriptorTable {
			HLSL::Texture2D<float4> transmittance_lut       = VirtualResourceID::TransmittanceLut;
			HLSL::Texture2D<float4> multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
			HLSL::Texture2D<float4> sky_panorama_lut        = VirtualResourceID::SkyPanoramaLut;
		};
		HLSL::DescriptorTable<ImGuiDescriptorTable> root_descriptor_table = { 0, 3 };
		
		auto& descriptor_table = AllocateDescriptorTable(&record_context, root_descriptor_table);
		
		{
			ImGui::Begin("Transmittance LUT");
			auto size = resource_table.virtual_resources[(u32)VirtualResourceID::TransmittanceLut].texture.size;
			ImGui::Image(descriptor_table.descriptor_heap_offset, ImVec2(size.x, size.y));
			ImGui::End();
		}
		
		{
			ImGui::Begin("Multiple Scattering LUT");
			auto size = resource_table.virtual_resources[(u32)VirtualResourceID::MultipleScatteringLut].texture.size;
			ImGui::Image(descriptor_table.descriptor_heap_offset + 1, ImVec2(size.x, size.y));
			ImGui::End();
		}
		
		{
			ImGui::Begin("Sky Panorma LUT");
			auto size = resource_table.virtual_resources[(u32)VirtualResourceID::SkyPanoramaLut].texture.size;
			ImGui::Image(descriptor_table.descriptor_heap_offset + 2, ImVec2(size.x, size.y) * 4.f);
			ImGui::End();
		}
		
		if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_F4)) {
			window->should_close = true;
		}
		
		TransmittanceLutRenderPass{}.RecordPass(&record_context);
		MultipleScatteringLutRenderPass{}.RecordPass(&record_context);
		SkyPanoramaLutRenderPass{}.RecordPass(&record_context);
		ImGuiRenderPass{}.RecordPass(&record_context);
		
		WindowSwapChainEndFrame(swap_chain, graphics_context, &alloc, record_context);
		
		frame_allocation_size = (alloc.total_allocated_size - frame_initial_size);
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
