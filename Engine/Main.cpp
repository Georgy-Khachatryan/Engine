#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicThreads.h"
#include "Editor/EditorEntities.h"
#include "Editor/LevelEditor.h"
#include "Entities.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/AsyncTransferQueue.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "ImGuiCustomWidgets.h"
#include "Renderer/Renderer.h"
#include "SystemWindow.h"
#include "UndoRedoSystem.h"


static void ApplicationStatisticsWindow(WorldEntitySystem& world_system, AssetEntitySystem& asset_system, u64 world_entity_guid, u64 frame_initial_size, u64 frame_allocation_size, u64 transient_upload_allocation_size, u64 transient_readback_allocation_size, u64 imgui_heap_size, s32* swap_chain_format_index) {
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, world_entity_guid);
	auto& meshlet_culling_statistics = world_entity.renderer_world->meshlet_culling_statistics;
	
	ImGui::Begin("Stats");
	ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
	ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
	ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
	ImGui::Text("Readback Alloc Size: %llu", transient_readback_allocation_size);
	ImGui::Text("ImGui Heap Size: %llu", imgui_heap_size);
	ImGui::Text("World System Heap Size: %llu", world_system.heap.ComputeTotalMemoryUsage());
	ImGui::Text("Asset System Heap Size: %llu", asset_system.heap.ComputeTotalMemoryUsage());
	ImGui::Text("Meshlet Count Raster Passes: %llu", meshlet_culling_statistics.meshlet_count);
	ImGui::Text("Meshlet Count Main Pass: %llu", meshlet_culling_statistics.meshlet_count_main_pass);
	ImGui::Text("Meshlet Count Disocclusion Pass: %llu", meshlet_culling_statistics.meshlet_count_disocclusion_pass);
	ImGui::Text("Meshlet Count Raytracing Pass: %llu", meshlet_culling_statistics.meshlet_count_raytracing_pass);
	ImGui::Combo("Swap Chain Format", swap_chain_format_index, "HDR\0SDR\0");
	ImGui::End();
}

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
	
	auto* icon_resource_table = CreateResourceTable(&alloc);
	defer{ ReleaseResourceTable(graphics_context, icon_resource_table); };
	
	WorldEntitySystem world_system;
	InitializeEntitySystem(world_system, &alloc);
	defer{ ReleaseHeapAllocator(world_system.heap); };
	
	WorldEntitySystem icon_world_system;
	InitializeEntitySystem(icon_world_system, &alloc);
	defer{ ReleaseHeapAllocator(icon_world_system.heap); };
	
	AssetEntitySystem asset_system;
	InitializeEntitySystem(asset_system, &alloc);
	defer{ ReleaseHeapAllocator(asset_system.heap); };
	
	UndoRedoSystem undo_redo_system;
	InitializeUndoRedoSystem(undo_redo_system, &imgui_heap);
	defer{ ReleaseUndoRedoSystem(undo_redo_system); };
	
	LevelEditorIO level_editor_io;
	u64 world_entity_guid = LoadOrCreateDefaultEntitySystems(&alloc, world_system, asset_system);
	
	CreateEditorIconCache(&alloc, icon_world_system, level_editor_io);
	
	u64 frame_allocation_size = 0;
	u64 transient_upload_allocation_size   = 0;
	u64 transient_readback_allocation_size = 0;
	while (window->should_close == false) {
		ProfilerScope("Frame");
		
		TempAllocationScopeNamed(frame_initial_size, &alloc);
		
		SystemPollWindowEvents(window);
		ResizeWindowSwapChain(swap_chain, graphics_context, window->size, swap_chain_formats[swap_chain_format_index]);
		WindowSwapChainBeginFrame(swap_chain, graphics_context, &alloc);
		ImGuiBeginFrame(window);
		RendererBeginFrame(renderer_context);
		
		ApplicationStatisticsWindow(world_system, asset_system, world_entity_guid, frame_initial_size, frame_allocation_size, transient_upload_allocation_size, transient_readback_allocation_size, imgui_heap.ComputeTotalMemoryUsage(), &swap_chain_format_index);
		
		Array<LevelEditorView> level_editor_views;
		LevelEditorUpdate(&alloc, graphics_context, undo_redo_system, world_system, asset_system, level_editor_io, world_entity_guid, level_editor_views);
		
		Array<RecordContext*> record_contexts;
		ArrayReserve(record_contexts, &alloc, level_editor_views.count);
		
		for (u32 view_index = 0; view_index < level_editor_views.count; view_index += 1) {
			auto& view = level_editor_views[view_index];
			
			// TODO: Better way to associate a resource table with a given view.
			Array<GpuComponentUploadBuffer> gpu_uploads;
			auto* record_context = BeginRecordContext(&alloc, renderer_context, swap_chain, view_index == 0 ? resource_table : icon_resource_table);
			defer{ EndRecordContext(&alloc, record_context, renderer_context, record_contexts); };
			
			// Update shared asset_system:
			if (view_index == 0) {
				UpdateAssetStreamingSystems(renderer_context, thread_pool, record_context, asset_system);
				
				UpdateEditorAssetComponents(&alloc, asset_system);
				UpdateRendererAssetGpuComponents(&alloc, record_context, asset_system, gpu_uploads);
			}
			
			// Update world_system:
			{
				UpdateWorldSystemReadback(record_context, *view.world_system, view.world_entity_guid);
				UpdateEntityGpuComponents(&alloc, record_context, *view.world_system, asset_system, gpu_uploads);
			}
			
			BuildRenderPassesForFrame(renderer_context, record_context, view.world_system, &asset_system, view.world_entity_guid, gpu_uploads, view_index, (u32)level_editor_views.count);
		}
		
		WindowSwapChainEndFrame(swap_chain, graphics_context, &alloc, record_contexts);
		
		ReleaseEntityComponents(&alloc, asset_system);
		for (auto& view : level_editor_views) {
			ReleaseEntityComponents(&alloc, *view.world_system);
		}
		
		ClearEntityMasks(asset_system);
		for (auto& view : level_editor_views) {
			ClearEntityMasks(*view.world_system);
		}
		
		frame_allocation_size = (alloc.total_allocated_size - frame_initial_size);
		transient_upload_allocation_size   = renderer_context->upload_buffer_offset;
		transient_readback_allocation_size = renderer_context->readback_buffer_offset;
	}
	WaitForInFlightSubmits(graphics_context);
	
	ReleaseTextureAssets(&alloc, graphics_context, asset_system);
	ReleaseEntitySystemGpuStreamAllocations(graphics_context, asset_system);
	ReleaseEntitySystemGpuStreamAllocations(graphics_context, world_system);
	ReleaseEntitySystemGpuStreamAllocations(graphics_context, icon_world_system);
	
	return 0;
}
