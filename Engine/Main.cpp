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


static void ApplicationStatisticsWindow(AssetEntitySystem& asset_system, u64 world_system_heap_size, u64 frame_initial_size, u64 frame_allocation_size, u64 transient_upload_allocation_size, u64 transient_readback_allocation_size, u64 imgui_heap_size, s32* swap_chain_format_index) {
	ImGui::Begin("Stats");
	ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
	ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
	ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
	ImGui::Text("Readback Alloc Size: %llu", transient_readback_allocation_size);
	ImGui::Text("ImGui Heap Size: %llu", imgui_heap_size);
	ImGui::Text("World System Heap Size: %llu", world_system_heap_size);
	ImGui::Text("Asset System Heap Size: %llu", asset_system.heap.ComputeTotalMemoryUsage());
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
	
	AssetEntitySystem asset_system;
	InitializeEntitySystem(asset_system, &alloc);
	defer{
		ReleaseTextureAssets(&alloc, graphics_context, asset_system);
		ReleaseEntitySystemGpuStreamAllocations(graphics_context, asset_system);
		ReleaseHeapAllocator(asset_system.heap);
	};
	
	UndoRedoSystem undo_redo_system;
	InitializeUndoRedoSystem(undo_redo_system, &imgui_heap);
	defer{ ReleaseUndoRedoSystem(undo_redo_system); };
	
	auto* level_editor = CreateLevelEditor(&alloc, graphics_context, asset_system);
	defer{ ReleaseLevelEditor(level_editor, graphics_context); };
	
	auto* icon_cache = CreateEditorIconCache(&alloc, graphics_context);
	defer{ ReleaseEditorIconCache(icon_cache, graphics_context); };
	
	LevelEditorIO level_editor_io;
	level_editor_io.icon_cache   = icon_cache;
	level_editor_io.level_editor = level_editor;
	
	u64 frame_allocation_size  = 0;
	u64 world_system_heap_size = 0;
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
		
		ApplicationStatisticsWindow(asset_system, world_system_heap_size, frame_initial_size, frame_allocation_size, transient_upload_allocation_size, transient_readback_allocation_size, imgui_heap.ComputeTotalMemoryUsage(), &swap_chain_format_index);
		world_system_heap_size = 0;
		
		Array<EditorWorldView> editor_world_views;
		LevelEditorUpdate(&alloc, graphics_context, undo_redo_system, asset_system, level_editor_io, editor_world_views);
		
		Array<RecordContext*> record_contexts;
		ArrayReserve(record_contexts, &alloc, editor_world_views.count);
		
		for (u32 view_index = 0; view_index < editor_world_views.count; view_index += 1) {
			auto& view = editor_world_views[view_index];
			
			Array<GpuComponentUploadBuffer> gpu_uploads;
			auto* record_context = BeginRecordContext(&alloc, renderer_context, swap_chain, view.resource_table);
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
			
			BuildRenderPassesForFrame(renderer_context, record_context, view.world_system, &asset_system, view.world_entity_guid, gpu_uploads, view_index, (u32)editor_world_views.count);
			
			world_system_heap_size += view.world_system->heap.ComputeTotalMemoryUsage();
		}
		
		WindowSwapChainEndFrame(swap_chain, graphics_context, &alloc, record_contexts);
		
		ReleaseEntityComponents(&alloc, asset_system);
		for (auto& view : editor_world_views) {
			ReleaseEntityComponents(&alloc, *view.world_system);
		}
		
		ClearEntityMasks(asset_system);
		for (auto& view : editor_world_views) {
			ClearEntityMasks(*view.world_system);
		}
		
		frame_allocation_size = (alloc.total_allocated_size - frame_initial_size);
		transient_upload_allocation_size   = renderer_context->upload_buffer_offset;
		transient_readback_allocation_size = renderer_context->readback_buffer_offset;
	}
	WaitForInFlightSubmits(graphics_context);
	
	return 0;
}
