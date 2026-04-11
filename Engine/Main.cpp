#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicThreads.h"
#include "Editor/LevelEditor.h"
#include "Entities.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "ImGuiCustomWidgets.h"
#include "Renderer/Renderer.h"
#include "SystemWindow.h"


s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	auto* thread_pool = CreateThreadPool(&alloc);
	defer{ ReleaseThreadPool(thread_pool); };
	
	extern void BasicExamples(StackAllocator* alloc);
	BasicExamples(&alloc);
	
	auto imgui_heap = CreateHeapAllocator(2 * 1024 * 1024);
	defer{ ReleaseHeapAllocator(imgui_heap); };
	
	ImGuiInitializeContext(&imgui_heap);
	
	auto* window = SystemCreateWindow(&alloc, "Engine"_sl);
	defer{ SystemReleaseWindow(window); };
	
	ImGuiInitializeWindow(window);
	
	auto* renderer_context = CreateRendererContext(&alloc);
	defer{ ReleaseRendererContext(renderer_context, &alloc); };
	
	defer{ ImGuiReleaseContext(renderer_context->graphics_context); };
	
	
	s32 swap_chain_format_index = 0;
	compile_const TextureFormat swap_chain_formats[2] = { TextureFormat::R16G16B16A16_FLOAT, TextureFormat::R8G8B8A8_UNORM_SRGB };
	
	auto* graphics_context = renderer_context->graphics_context;
	auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, swap_chain_formats[swap_chain_format_index]);
	defer{ ReleaseWindowSwapChain(swap_chain, graphics_context); };
	
	auto* resource_table = CreateResourceTable(&alloc);
	defer{ ReleaseResourceTable(graphics_context, resource_table); };
	
	WorldEntitySystem world_system;
	InitializeEntitySystem(world_system);
	defer{ ReleaseHeapAllocator(world_system.heap); };
	
	AssetEntitySystem asset_system;
	InitializeEntitySystem(asset_system);
	defer{ ReleaseHeapAllocator(asset_system.heap); };
	
	auto* editor_context = CreateLevelEditorContext(&alloc, &imgui_heap, world_system, asset_system);
	defer{ ReleaseLevelEditorContext(editor_context); };
	
	auto world_entity = ExtractComponentStreams<WorldEntityType>(QueryEntityTypeArray<WorldEntityType>(world_system));
	u64 world_entity_guid = world_entity.guid->guid;
	
	u64 frame_allocation_size = 0;
	u64 transient_upload_allocation_size = 0;
	while (window->should_close == false) {
		ProfilerScope("Frame");
		
		TempAllocationScopeNamed(frame_initial_size, &alloc);
		
		SystemPollWindowEvents(window);
		ResizeWindowSwapChain(swap_chain, graphics_context, window->size, swap_chain_formats[swap_chain_format_index]);
		WindowSwapChainBeginFrame(swap_chain, graphics_context, &alloc);
		ImGuiBeginFrame(window);
		
		auto& meshlet_culling_statistics = world_entity.renderer_world->meshlet_culling_statistics;
		
		ImGui::Begin("Stats");
		ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
		ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
		ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
		ImGui::Text("ImGui Heap Size: %llu", imgui_heap.ComputeTotalMemoryUsage());
		ImGui::Text("World System Heap Size: %llu", world_system.heap.ComputeTotalMemoryUsage());
		ImGui::Text("Asset System Heap Size: %llu", asset_system.heap.ComputeTotalMemoryUsage());
		ImGui::Text("Meshlet Count: %llu", meshlet_culling_statistics.meshlet_count);
		ImGui::Text("Meshlet Count Main Pass: %llu", meshlet_culling_statistics.meshlet_count_main_pass);
		ImGui::Text("Meshlet Count Disocclusion Pass: %llu", meshlet_culling_statistics.meshlet_count_disocclusion_pass);
		ImGui::Combo("Swap Chain Format", &swap_chain_format_index, "HDR\0SDR\0");
		ImGui::End();
		
		auto* record_context = BeginRecordContext(&alloc, renderer_context, swap_chain, resource_table);
		LevelEditorUpdate(editor_context, &alloc, record_context, world_system, asset_system, world_entity_guid);
		
		Array<GpuComponentUploadBuffer> gpu_uploads;
		UpdateStreamingSystems(renderer_context, thread_pool, record_context, &world_system, &asset_system, world_entity_guid);
		UpdateEntityGpuComponents(&alloc, record_context, world_system, asset_system, gpu_uploads);
		UpdateRendererEntityGpuComponents(&alloc, thread_pool, renderer_context->async_transfer_queue, record_context, asset_system, gpu_uploads);
		UpdateAsyncTransferQueue(renderer_context->async_transfer_queue);
		
		world_entity.renderer_world->gpu_uploads = gpu_uploads;
		
		BuildRenderPassesForFrame(renderer_context, record_context, &world_system, &asset_system, world_entity_guid);
		WindowSwapChainEndFrame(swap_chain, graphics_context, &alloc, record_context);
		
		ReleaseEntityComponents(&alloc, world_system, asset_system);
		
		ClearEntityMasks(world_system);
		ClearEntityMasks(asset_system);
		
		frame_allocation_size = (alloc.total_allocated_size - frame_initial_size);
		transient_upload_allocation_size = record_context->upload_buffer_offset;
	}
	WaitForInFlightSubmits(graphics_context);
	
	ReleaseTextureAssets(&alloc, graphics_context, asset_system);
	
	return 0;
}
