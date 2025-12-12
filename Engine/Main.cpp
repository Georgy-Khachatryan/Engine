#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicHashTable.h"
#include "SystemWindow.h"
#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "EntitySystem.h"
#include "Entities.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/imgui_internal.h>
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
			auto* memory0 = heap.Allocate(128, 64);
			auto* memory1 = heap.Allocate(128, 64);
			auto* memory2 = heap.Allocate(128, 64);
			
			DebugAssert((u64)memory0 % 64 == 0, "Incorrect heap allocation alignment. Offset: %llu.", (u64)memory0 % 64);
			DebugAssert((u64)memory1 % 64 == 0, "Incorrect heap allocation alignment. Offset: %llu.", (u64)memory1 % 64);
			DebugAssert((u64)memory2 % 64 == 0, "Incorrect heap allocation alignment. Offset: %llu.", (u64)memory2 % 64);
			
			memset(memory0, 0xA0, 128);
			memset(memory1, 0xA1, 128);
			memset(memory2, 0xA2, 128);
			
			heap.Deallocate(memory0);
			memory1 = heap.Reallocate(memory1, 128, 192, 64);
			DebugAssert(memory1 == memory0, "Reallocation failed.");
			DebugAssert((u64)memory1 % 64 == 0, "Incorrect heap allocation alignment. Offset: %llu.", (u64)memory1 % 64);
			
			for (u32 i = 0; i < 128; i += 1) {
				u32 byte = ((u8*)memory1)[i];
				DebugAssert(byte == 0xA1, "Reallocation copy failed. 0x%x != 0xA1.", byte);
			}
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
	compile_const u32 upload_buffer_size = 32 * 1024 * 1024;
	
	for (u32 i = 0; i < number_of_frames_in_flight; i += 1) {
		upload_buffers[i] = CreateBufferResource(graphics_context, upload_buffer_size, GpuMemoryAccessType::Upload, &upload_buffer_cpu_addresses[i]);
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
	float meshlet_target_error_pixels = 1.f;
	bool use_perspective_view_to_clip = true;
	
	quat world_to_view_quat = Math::AxisAngleToQuat(float3(1.f, 0.f, 0.f), 90.f * Math::degrees_to_radians) * Math::AxisAngleToQuat(float3(0.f, 0.f, 1.f), 90.f * Math::degrees_to_radians);
	float3 world_space_camera_position = 0.f;
	
	
	EntitySystem entity_system;
	InitializeEntitySystem(entity_system);
	defer{ ReleaseHeapAllocator(entity_system.heap); };
	
	CreateEntities<MeshEntityType>(&alloc, entity_system, 64);
	CreateEntities<CameraEntityType>(&alloc, entity_system, 64);
	
	
	u64  random_seed    = 0x7C7C4065B00D53D3ull;
	bool random_success = _rdrand64_step(&random_seed) != 0;
	DebugAssert(random_success, "Failed to initialize random number generator.");
	
	{
		auto guid_view = QueryEntities<GuidQuery>(&alloc, entity_system);
		
		u32 entity_count = 0;
		for (auto* entity_array : guid_view) {
			entity_count += entity_array->count;
		}
		
		HashTableReserve(entity_system.entity_guid_to_entity_id, &entity_system.heap, entity_count);
		
		for (auto* entity_array : guid_view) {
			auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
			
			auto* entity_ids = entity_array->stream_index_to_entity_id;
			for (u32 index = 0; index < entity_array->count; index += 1) {
				auto& [guid] = streams.guid[index];
				u32 entity_id = entity_ids[index];
				
				guid = GenerateRandomNumber64(random_seed);
				HashTableAddOrFind(entity_system.entity_guid_to_entity_id, guid, entity_id);
			}
		}
		
		for (auto* entity_array : QueryEntities<NameQuery>(&alloc, entity_system)) {
			auto streams = ExtractComponentStreams<NameQuery>(entity_array);
			
			for (auto& [name] : ArrayView<NameComponent>{ streams.name, entity_array->count }) {
				name = {};
			}
		}
	}
	
	{
		float position_index = 0.f;
		for (auto* entity_array : QueryEntities<PositionQuery>(&alloc, entity_system)) {
			auto streams = ExtractComponentStreams<PositionQuery>(entity_array);
			
			for (auto& [position] : ArrayView<PositionComponent>{ streams.position, entity_array->count }) {
				position = float3(position_index, 0.f, 0.f);
				position_index += 1.f;
			}
		}
		
		for (auto* entity_array : QueryEntities<RotationQuery>(&alloc, entity_system)) {
			auto streams = ExtractComponentStreams<RotationQuery>(entity_array);
			
			for (auto& [rotation] : ArrayView<RotationComponent>{ streams.rotation, entity_array->count }) {
				rotation = quat();
			}
		}
		
		for (auto* entity_array : QueryEntities<ScaleQuery>(&alloc, entity_system)) {
			auto streams = ExtractComponentStreams<ScaleQuery>(entity_array);
			
			for (auto& [scale] : ArrayView<ScaleComponent>{ streams.scale, entity_array->count }) {
				scale = 1.f;
			}
		}
	}
	
	HashTable<u64, EntityTypeID> selected_entities_hash_table;
	
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
		resource_table.Set(VirtualResourceID::VisibleMeshlets, (u32)(meshlets.count * sizeof(u32)));
		resource_table.Set(VirtualResourceID::MeshletIndirectArguments, sizeof(uint4));
		
		struct ImGuiDescriptorTable : HLSL::BaseDescriptorTable {
			HLSL::Texture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
		};
		HLSL::DescriptorTable<ImGuiDescriptorTable> root_descriptor_table = { 0, 1 };
		
		auto& descriptor_table = AllocateDescriptorTable(&record_context, root_descriptor_table);
		
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::Begin("Scene");
		auto window_size = float2(ImGui::GetContentRegionAvail());
		auto window_pos  = ImGui::GetWindowPos();
		ImGui::Image(descriptor_table.descriptor_heap_offset, ImVec2(window_size.x, window_size.y));
		bool scene_hovered = ImGui::IsWindowHovered();
		bool scene_focused = ImGui::IsWindowFocused();
		
		ImRect mouse_lock_rect;
		mouse_lock_rect.Min = window_pos;
		mouse_lock_rect.Max = window_pos + ImGui::GetWindowSize();
		mouse_lock.Update(ImGuiMouseButton_Left,  scene_hovered, mouse_lock_rect);
		mouse_lock.Update(ImGuiMouseButton_Right, scene_hovered, mouse_lock_rect);
		
		ImGui::End();
		ImGui::PopStyleVar();
		
		// Clamp render target size to a reasonable minimum. Aspect ratio for view to clip is still computed using unclamped values.
		uint2 render_target_size = uint2((u32)Max(window_size.x, 16.f), (u32)Max(window_size.y, 16.f));
		resource_table.Set(VirtualResourceID::SceneRadiance, TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size));
		resource_table.Set(VirtualResourceID::DepthStencil,  TextureSize(TextureFormat::D32_FLOAT, render_target_size));
		
		compile_const float near_depth = 0.1f;
		
		ImGui::Begin("Stats");
		ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
		ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
		ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
		ImGui::Text("ImGui Alloc Size: %llu", imgui_heap.ComputeTotalMemoryUsage());
		ImGui::Checkbox("Perspective Camera", &use_perspective_view_to_clip);
		ImGui::SliderFloat("Vertical Field Of View", &vertical_fov_degrees, 10.f, 135.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Meshlet Target Error Pixels", &meshlet_target_error_pixels, 1.f, 128.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Sun Elevation", &sun_elevation_degrees, -10.f, +190.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
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
									HashTableAddOrFind(selected_entities_hash_table, &entity_system.heap, guid, entity_array->entity_type_id);
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
									HashTableAddOrFind(selected_entities_hash_table, &entity_system.heap, guid, entity_array->entity_type_id);
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
						
						bool is_selected = HashTableFind(selected_entities_hash_table, guid) != nullptr;
						
						ImGui::SetNextItemSelectionUserData(index);
						ImGui::Selectable(name.count ? name.data : "EmptyName", is_selected);
					}
				}
			}
			
			ms_io = ImGui::EndMultiSelect();
			apply_requests(ms_io);
			
			if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
				for (auto& [guid, entity_type_id] : selected_entities_hash_table) {
					auto* guid_to_id = HashTableRemove(entity_system.entity_guid_to_entity_id, guid);
					if (guid_to_id != nullptr) {
						RemoveEntity(entity_system, entity_type_id, guid_to_id->value);
					}
				}
				HashTableClear(selected_entities_hash_table);
			}
		}
		ImGui::EndGroup();
		
		ImGui::End();
		
		if (scene_focused) {
			float base_speed = 10.f; // m/s
			float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
			
			auto world_to_view = Math::QuatToRotationMatrix(world_to_view_quat);
			float move_distance = base_speed * sensetivity_scale * io.DeltaTime;
			if (ImGui::IsKeyDown(ImGuiKey_D)) world_space_camera_position += world_to_view[0] * +move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_A)) world_space_camera_position += world_to_view[0] * -move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_W)) world_space_camera_position += world_to_view[2] * +move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_S)) world_space_camera_position += world_to_view[2] * -move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_Q)) world_space_camera_position += world_to_view[1] * +move_distance;
			if (ImGui::IsKeyDown(ImGuiKey_E)) world_space_camera_position += world_to_view[1] * -move_distance;
		}
		
		if (scene_hovered && io.MouseWheel != 0.f && mouse_lock.locked_mouse_button == ImGuiMouseButton_COUNT) {
			float4 view_to_clip_coef;
			if (use_perspective_view_to_clip) {
				view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
			} else {
				view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
			}
			auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
			
			auto uv = float2(ImGui::GetMousePos() - window_pos) / float2(window_size);
			auto ray_info = Math::RayInfoFromScreenUv(uv, clip_to_view_coef);
			
			auto view_to_world = Math::Transpose(Math::QuatToRotationMatrix(world_to_view_quat));
			
			float meters_per_click = 1.f;
			float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
			
			float move_distance = io.MouseWheel * meters_per_click * sensetivity_scale;
			world_space_camera_position += (view_to_world * ray_info.direction) * move_distance;
		}
		
		if (mouse_lock.locked_mouse_button == ImGuiMouseButton_Left) {
			float radians_per_pixel = 1.f / 240.f;
			
			compile_const float3 view_space_up = float3(0.f, -1.f, 0.f);
			auto world_space_up = Math::Conjugate(world_to_view_quat) * view_space_up;
			world_to_view_quat *= Math::AxisAngleToQuat(float3(0.f, 0.f, world_space_up.z < 0.f ? -1.f : 1.f), io.MouseDelta.x * radians_per_pixel);
			
			// Compute view_to_world after we applied rotation around Z axis.
			auto view_to_world = Math::Transpose(Math::QuatToRotationMatrix(world_to_view_quat));
			world_to_view_quat *= Math::AxisAngleToQuat(view_to_world * float3(1.f, 0.f, 0.f), io.MouseDelta.y * radians_per_pixel);
			
			world_to_view_quat = Math::Normalize(world_to_view_quat);
		} else if (mouse_lock.locked_mouse_button == ImGuiMouseButton_Right) {
			float meters_per_pixel = 1.f / 240.f;
			float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
			
			auto world_to_view = Math::QuatToRotationMatrix(world_to_view_quat);
			world_space_camera_position += world_to_view[0] * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.x);
			world_space_camera_position += world_to_view[1] * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.y);
		}
		
		SceneConstants scene;
		scene.render_target_size     = float2(render_target_size);
		scene.inv_render_target_size = float2(1.f) / scene.render_target_size;
		if (use_perspective_view_to_clip) {
			scene.view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
			scene.clip_to_view_coef = Math::ViewToClipInverse(scene.view_to_clip_coef);
		} else {
			scene.view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
			scene.clip_to_view_coef = Math::ViewToClipInverse(scene.view_to_clip_coef);
		}
		
		auto world_to_view_rotation = Math::QuatToRotationMatrix(world_to_view_quat);
		auto view_space_camera_position = world_to_view_rotation * world_space_camera_position;
		scene.world_to_view[0] = float4(world_to_view_rotation[0], -view_space_camera_position[0]);
		scene.world_to_view[1] = float4(world_to_view_rotation[1], -view_space_camera_position[1]);
		scene.world_to_view[2] = float4(world_to_view_rotation[2], -view_space_camera_position[2]);
		
		auto view_to_world_rotation = Math::Transpose(world_to_view_rotation);
		scene.view_to_world[0] = float4(view_to_world_rotation[0], world_space_camera_position[0]);
		scene.view_to_world[1] = float4(view_to_world_rotation[1], world_space_camera_position[1]);
		scene.view_to_world[2] = float4(view_to_world_rotation[2], world_space_camera_position[2]);
		
		scene.world_to_pixel_scale = scene.view_to_clip_coef.x * scene.render_target_size.x * 0.5f / Max(meshlet_target_error_pixels, 1.f);
		scene.world_space_camera_position = world_space_camera_position;
		
		AtmosphereParameters atmosphere_parameters;
		atmosphere_parameters.world_space_sun_direction.x = cosf(sun_elevation_degrees * Math::degrees_to_radians);
		atmosphere_parameters.world_space_sun_direction.z = sinf(sun_elevation_degrees * Math::degrees_to_radians);
		
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
			auto [ib_gpu_address, ib_cpu_address] = AllocateTransientUploadBuffer<u8, sizeof(uint4)>(&record_context, (u32)indices.count);
			memcpy(vb_cpu_address, vertices.data, vertices.count * sizeof(BasicVertex));
			memcpy(mb_cpu_address, meshlets.data, meshlets.count * sizeof(BasicMeshlet));
			memcpy(ib_cpu_address, indices.data,  indices.count  * sizeof(u8));
			
			MeshletClearBuffersRenderPass{}.RecordPass(&record_context);
			MeshletCullingRenderPass{ scene_constants_gpu_address, mb_gpu_address, (u32)meshlets.count }.RecordPass(&record_context);
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
