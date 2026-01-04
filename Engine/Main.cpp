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
#include "ImGuiCustomWidgets.h"
#include "RenderPasses.h"
#include "SystemWindow.h"

#include <SDK/imgui/imgui.h>
#include <SDK/imgui/imgui_internal.h>


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

static void CameraControls(CameraEntityType camera_entity, bool scene_focused, bool scene_hovered, ImGuiMouseLock& mouse_lock, float2 window_pos, float2 window_size) {
	auto& view_to_world_quat   = camera_entity.rotation->rotation;
	auto& world_space_position = camera_entity.position->position;
	
	auto& io = ImGui::GetIO();
	
	if (scene_focused) {
		float base_speed = 10.f; // m/s
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		auto  world_to_view = Math::QuatToRotationMatrix(Math::Conjugate(view_to_world_quat));
		float move_distance = base_speed * sensetivity_scale * io.DeltaTime;
		if (ImGui::IsKeyDown(ImGuiKey_D, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r0 * +move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_A, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r0 * -move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_W, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r2 * +move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_S, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r2 * -move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_Q, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r1 * +move_distance;
		if (ImGui::IsKeyDown(ImGuiKey_E, ImGuiKeyOwner_NoOwner)) world_space_position += world_to_view.r1 * -move_distance;
	}
	
	if (scene_hovered && io.MouseWheel != 0.f && mouse_lock.locked_mouse_button == ImGuiMouseButton_COUNT) {
		float vertical_fov_degrees = camera_entity.camera->vertical_fov_degrees;
		float near_depth           = camera_entity.camera->near_depth;
		
		float4 view_to_clip_coef;
		if (camera_entity.camera->transform_type == CameraTransformType::Perspective) {
			view_to_clip_coef = Math::PerspectiveViewToClip(vertical_fov_degrees * Math::degrees_to_radians, window_size, near_depth);
		} else {
			view_to_clip_coef = Math::OrthographicViewToClip(window_size * vertical_fov_degrees * (1.f / window_size.x), 1024.f);
		}
		auto clip_to_view_coef = Math::ViewToClipInverse(view_to_clip_coef);
		
		auto uv = (float2(ImGui::GetMousePos()) - window_pos) / window_size;
		auto ray_info = Math::RayInfoFromScreenUv(uv, clip_to_view_coef);
		
		auto view_to_world = Math::QuatToRotationMatrix(view_to_world_quat);
		
		float meters_per_click = 1.f;
		float sensetivity_scale = (ImGui::IsKeyDown(ImGuiMod_Shift) ? 5.f : 1.f) * (ImGui::IsKeyDown(ImGuiMod_Alt) ? 0.2f : 1.f);
		
		float move_distance = io.MouseWheel * meters_per_click * sensetivity_scale;
		world_space_position += (view_to_world * ray_info.direction) * move_distance;
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
		world_space_position += world_to_view.r0 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.x);
		world_space_position += world_to_view.r1 * ((meters_per_pixel * sensetivity_scale) * io.MouseDelta.y);
	}
}

s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	extern void BasicExamples(StackAllocator* alloc);
	BasicExamples(&alloc);
	
	auto imgui_heap = CreateHeapAllocator(2 * 1024 * 1024);
	defer{ ReleaseHeapAllocator(imgui_heap); };
	
	ImGuiInitializeContext(&imgui_heap);
	
	auto* window = SystemCreateWindow(&alloc, "Engine"_sl);
	defer{ SystemReleaseWindow(window); };
	
	ImGuiInitializeWindow(window);
	
	auto* graphics_context = CreateGraphicsContext(&alloc);
	defer{ ReleaseGraphicsContext(graphics_context, &alloc); };
	defer{ ImGuiReleaseContext(graphics_context); };
	
	// TODO: Dynamically switch between HDR and SDR, add tone mappers for both.
	auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R16G16B16A16_FLOAT);
	// auto* swap_chain = CreateWindowSwapChain(&alloc, graphics_context, window->hwnd, TextureFormat::R8G8B8A8_UNORM_SRGB);
	defer{ ReleaseWindowSwapChain(swap_chain, graphics_context); };
	
	
	FixedCountArray<NativeBufferResource, number_of_frames_in_flight> upload_buffers;
	FixedCountArray<u8*, number_of_frames_in_flight> upload_buffer_cpu_addresses;
	compile_const u32 upload_buffer_size = 8 * 1024 * 1024;
	
	for (u32 i = 0; i < number_of_frames_in_flight; i += 1) {
		upload_buffers[i] = CreateBufferResource(graphics_context, upload_buffer_size, GpuMemoryAccessType::Upload, &upload_buffer_cpu_addresses[i]);
	}
	u32 upload_buffer_index = 0;
	
	compile_const u32 mesh_asset_buffer_size = 32 * 1024 * 1024;
	u8* mesh_asset_buffer_cpu_address = nullptr;
	u32 mesh_asset_buffer_offset = 0;
	auto mesh_asset_buffer = CreateBufferResource(graphics_context, mesh_asset_buffer_size, GpuMemoryAccessType::Upload, &mesh_asset_buffer_cpu_address);
	
	
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
			auto world_entity  = CreateEntities<WorldEntityType>(entity_system, 1);
			auto camera_entity = CreateEntities<CameraEntityType>(entity_system, 1);
			auto mesh_asset    = CreateEntities<MeshAssetType>(entity_system, 1);
			auto mesh_entities = CreateEntities<MeshEntityType>(entity_system, mesh_instance_count);
			
			{
				camera_entity.rotation->rotation =
					Math::AxisAngleToQuat(float3(0.f, 0.f, 1.f), -90.f * Math::degrees_to_radians) *
					Math::AxisAngleToQuat(float3(1.f, 0.f, 0.f), -90.f * Math::degrees_to_radians);
				
				camera_entity.name->name = StringCopy(&entity_system.heap, "Camera"_sl);
				world_entity.camera_entity->guid = camera_entity.guid->guid;
			}
			
			{
				extern MeshRuntimeDataLayout ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid);
				
				TempAllocationScope(&alloc);
				auto source_data_filepath = "./Assets/Source/Torus/Torus.fbx"_sl;
				u64 runtime_data_guid = GenerateRandomNumber64(entity_system.guid_random_seed);
				auto runtime_data_layout = ImportFbxMeshFile(&alloc, source_data_filepath, runtime_data_guid);
				
				auto runtime_data_filepath = StringFormat(&alloc, "./Assets/Runtime/%x..mrd"_sl, runtime_data_layout.file_guid);
				mesh_asset.name->name            = StringCopy(&entity_system.heap, "Torus"_sl);
				mesh_asset.source_data->filepath = StringCopy(&entity_system.heap, source_data_filepath);
				*mesh_asset.runtime_data_layout  = runtime_data_layout;
			}
			
			{
				compile_const float spacing = 2.f;
				compile_const float center_offset = -(float)mesh_grid_size * 0.5f * spacing;
				
				u64 mesh_asset_guid = mesh_asset.guid->guid;
				for (u32 i = 0; i < mesh_instance_count; i += 1) {
					mesh_entities.position[i].position = float3((i % mesh_grid_size) * spacing + center_offset, (i / mesh_grid_size) * spacing + center_offset, 0.f);
					mesh_entities.mesh_asset[i].guid = mesh_asset_guid;
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
		ProfilerScope("Frame");
		
		TempAllocationScopeNamed(frame_initial_size, &alloc);
		
		SystemPollWindowEvents(window);
		ResizeWindowSwapChain(swap_chain, graphics_context, window->size);
		WindowSwapChainBeginFrame(swap_chain, graphics_context, &alloc);
		ImGuiBeginFrame(window);
		
		
		RecordContext record_context;
		record_context.alloc   = &alloc;
		record_context.context = graphics_context;
		record_context.resource_table = &resource_table;
		
		resource_table.Set(VirtualResourceID::CurrentBackBuffer, WindowSwapGetCurrentBackBuffer(swap_chain), swap_chain->size);
		resource_table.Set(VirtualResourceID::MeshAssetBuffer, mesh_asset_buffer, mesh_asset_buffer_size, mesh_asset_buffer_cpu_address);
		resource_table.Set(VirtualResourceID::TransientUploadBuffer, upload_buffers[upload_buffer_index], upload_buffer_size, upload_buffer_cpu_addresses[upload_buffer_index]);
		upload_buffer_index = (upload_buffer_index + 1) % number_of_frames_in_flight;
		
		
		u32 scene_descriptor_heap_offset = CreateResourceDescriptor(&record_context, HLSL::Texture2D<float4>(VirtualResourceID::SceneRadiance));
		
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.f, 0.f));
		ImGui::Begin("Scene");
		
		auto window_size = ImGui::GetWindowSize();
		auto window_pos  = ImGui::GetWindowPos();
		ImGui::ImageButton("Scene", scene_descriptor_heap_offset, window_size);
		bool scene_hovered = ImGui::IsItemHovered();
		bool scene_focused = ImGui::IsItemFocused();
		
		auto mouse_lock_rect_min = window_pos;
		auto mouse_lock_rect_max = window_pos + window_size;
		mouse_lock.Update(ImGuiMouseButton_Left,  scene_hovered, mouse_lock_rect_min, mouse_lock_rect_max);
		mouse_lock.Update(ImGuiMouseButton_Right, scene_hovered, mouse_lock_rect_min, mouse_lock_rect_max);
		
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
		
		ImGui::Text("Initial Alloc Size: %llu", frame_initial_size);
		ImGui::Text("Frame Alloc Size: %llu", frame_allocation_size);
		ImGui::Text("Upload Alloc Size: %llu", transient_upload_allocation_size);
		ImGui::Text("ImGui Alloc Size: %llu", imgui_heap.ComputeTotalMemoryUsage());
		ImGui::SliderFloat("Meshlet Target Error Pixels", &world_entity.renderer_world->meshlet_target_error_pixels, 1.f, 128.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Sun Elevation", &world_entity.renderer_world->sun_elevation_degrees, -10.f, +190.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		
		{
			ProfilerScope("SceneView");
			
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
			
			
			if (ImGui::BeginTable("SceneView", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInner | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY)) {
				defer{ ImGui::EndTable(); };
				
				ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.f);
				ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch, 2.f);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.f);
				ImGui::TableHeadersRow();
				
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
						
						auto entity_type_name = entity_type_name_table[entity_array->entity_type_id.index];
						
						auto streams = ExtractComponentStreams<GuidNameQuery>(entity_array);
						for (u32 index = start_index; index < end_index; index += 1) {
							auto& [guid] = streams.guid[index - global_index];
							auto& [name] = streams.name[index - global_index];
							
							ImGui::TableNextRow();
							
							ImGuiScopeID((void*)guid);
							
							if (ImGui::TableSetColumnIndex(0)) {
								ImGui::Bullet();
								ImGui::SameLine();
								
								bool is_selected = HashTableFind(selected_entities_hash_table, guid) != nullptr;
								ImGui::SetNextItemSelectionUserData(index);
								ImGui::Selectable(name.count ? name.data : entity_type_name.data, is_selected, ImGuiSelectableFlags_SpanAllColumns);
							}
							
							if (ImGui::TableSetColumnIndex(1)) {
								ImGui::Text("0x%016llx", guid);
							}
							
							if (ImGui::TableSetColumnIndex(2)) {
								ImGui::TextUnformatted(entity_type_name.data);
							}
						}
					}
				}
				
				ms_io = ImGui::EndMultiSelect();
				apply_requests(ms_io);
			}
			
			
			if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
				for (auto& [guid] : selected_entities_hash_table) {
					RemoveEntityByGUID(entity_system, guid);
				}
				HashTableClear(selected_entities_hash_table);
			}
		}
		
		ImGui::End();
		
		if (selected_entities_hash_table.count != 0) {
			ImGui::Begin("Entity Editor", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
			defer{ ImGui::End(); };
			
			if (ImGui::BeginTable("Components", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY)) {
				defer{ ImGui::EndTable(); };
				
				ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.f);
				ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch, 3.f);
				ImGui::TableHeadersRow();
				
				// Items should span full width of the column.
				ImGui::PushItemWidth(-FLT_MIN);
				defer{ ImGui::PopItemWidth(); };
				
				u64 entity_guid = (*selected_entities_hash_table.begin()).key;
				ImGuiScopeID((void*)entity_guid);
				
				ImGui::BeginDisabled(selected_entities_hash_table.count >= 2);
				defer{ ImGui::EndDisabled(); };
				
				// TODO: Make use of QueryEntityByGUID.
				auto* element = HashTableFind(entity_system.entity_guid_to_entity_id, entity_guid);
				DebugAssert(element, "Failed to find entity by guid 0x%x.", entity_guid);
				
				auto typed_entity_id = element->value;
				auto* array = &entity_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
				u32 entity_stream_index = array->entity_id_to_stream_index[typed_entity_id.entity_id.index];
				auto entity = ExtractComponentStreams<EntityEditorQuery>(array, entity_stream_index);
				
				
				bool is_dirty = false;
				
				if (entity.guid) {
					ImGui::TableNextRow();
					if (ImGui::TableSetColumnIndex(0)) {
						ImGui::AlignTextToFramePadding();
						ImGui::Text("GUID");
					}
					
					if (ImGui::TableSetColumnIndex(1)) {
						auto guid_string = StringFormat(&alloc, "0x%"_sl, (void*)entity.guid->guid);
						ImGui::InputText("##GUID", guid_string, nullptr, ImGuiInputTextFlags_ReadOnly);
					}
				}
				
				if (entity.name) {
					ImGui::TableNextRow();
					if (ImGui::TableSetColumnIndex(0)) {
						ImGui::AlignTextToFramePadding();
						ImGui::Text("Name");
					}
					
					if (ImGui::TableSetColumnIndex(1)) {
						auto& name = entity.name->name;
						ImGui::InputText("##InputText", name, &entity_system.heap);
					}
				}
				
				if (entity.position) {
					ImGui::TableNextRow();
					if (ImGui::TableSetColumnIndex(0)) {
						ImGui::AlignTextToFramePadding();
						ImGui::Text("Position");
					}
					
					if (ImGui::TableSetColumnIndex(1)) {
						auto& position = entity.position->position;
						is_dirty |= ImGui::DragFloat3("##Position", &position.x, 0.1f);
					}
				}
				
				if (entity.rotation) {
					ImGui::TableNextRow();
					if (ImGui::TableSetColumnIndex(0)) {
						ImGui::AlignTextToFramePadding();
						ImGui::Text("Rotation");
					}
					
					if (ImGui::TableSetColumnIndex(1)) {
						auto& rotation = entity.rotation->rotation;
						if (ImGui::DragFloat4("##Rotation", &rotation.x, 0.1f)) { // TODO: Use Euler angles for UI.
							float length = Math::Length(rotation);
							if (length > 0.f) {
								rotation *= (1.f / length);
							} else {
								rotation = {};
							}
							is_dirty = true;
						}
					}
				}
				
				if (entity.scale) {
					ImGui::TableNextRow();
					if (ImGui::TableSetColumnIndex(0)) {
						ImGui::AlignTextToFramePadding();
						ImGui::Text("Scale");
					}
					
					if (ImGui::TableSetColumnIndex(1)) {
						is_dirty |= ImGui::DragFloat("##Scale", &entity.scale->scale, 0.1f, 0.f, 8.f);
					}
				}
				
				if (entity.mesh_asset) {
					ImGui::TableNextRow();
					if (ImGui::TableSetColumnIndex(0)) {
						ImGui::AlignTextToFramePadding();
						ImGui::Text("Mesh Asset");
					}
					
					if (ImGui::TableSetColumnIndex(1)) {
						auto guid_string = StringFormat(&alloc, "0x%x"_sl, entity.mesh_asset->guid);
						ImGui::InputText("##MeshAssetGUID", guid_string, nullptr, ImGuiInputTextFlags_ReadOnly);
					}
				}
				
				if (entity.camera) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns)) {
						ImGui::TableNextRow();
						if (ImGui::TableSetColumnIndex(0)) {
							ImGui::AlignTextToFramePadding();
							ImGui::Text("Camera Transform Type");
						}
						
						if (ImGui::TableSetColumnIndex(1)) {
							is_dirty |= ImGui::Combo("##Camera Transform Type", (s32*)&entity.camera->transform_type, "Perspective\0Orthographic\0");
						}
						
						ImGui::TableNextRow();
						if (ImGui::TableSetColumnIndex(0)) {
							ImGui::AlignTextToFramePadding();
							ImGui::Text("Vertical Field Of View");
						}
						
						if (ImGui::TableSetColumnIndex(1)) {
							is_dirty |= ImGui::SliderFloat("##Vertical Field Of View", &entity.camera->vertical_fov_degrees, 10.f, 135.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
						}
						
						ImGui::TableNextRow();
						if (ImGui::TableSetColumnIndex(0)) {
							ImGui::AlignTextToFramePadding();
							ImGui::Text("Camera Near Depth");
						}
						
						if (ImGui::TableSetColumnIndex(1)) {
							is_dirty |= ImGui::SliderFloat("##Camera Near Depth", &entity.camera->near_depth, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
						}
					}
				}
				
				if (is_dirty) {
					BitArraySetBit(array->dirty_mask, entity_stream_index);
				}
			}
		}
		
		
		auto camera_entity = QueryEntityByGUID<CameraEntityType>(entity_system, world_entity.camera_entity->guid);
		CameraControls(camera_entity, scene_focused, scene_hovered, mouse_lock, float2(window_pos), float2(window_size));
		
		
		Array<GpuComponentUploadBuffer> gpu_uploads;
		for (auto* entity_array : QueryEntities<MeshEntityType>(&alloc, entity_system)) {
			ProfilerScope("MeshEntityGpuComponentUpdate");
			
			u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
			if (dirty_entity_count == 0) continue;
			
			auto streams = ExtractComponentStreams<MeshEntityType>(entity_array);
			
			auto gpu_transform = AllocateGpuComponentUploadBuffer(&record_context, dirty_entity_count, streams.gpu_transform);
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				GpuTransform transform;
				transform.position = streams.position[i].position;
				transform.scale    = streams.scale[i].scale;
				transform.rotation = streams.rotation[i].rotation;
				QueueGpuUpload(gpu_transform, (u32)i, transform);
			}
			ArrayAppend(gpu_uploads, &alloc, gpu_transform);
			
			auto gpu_mesh_entity_data = AllocateGpuComponentUploadBuffer(&record_context, dirty_entity_count, streams.gpu_mesh_entity_data);
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				auto* element = HashTableFind(entity_system.entity_guid_to_entity_id, streams.mesh_asset[i].guid);
				
				GpuMeshEntityData mesh_entity;
				mesh_entity.mesh_asset_entity_id = element ? element->value.entity_id.index : u32_max;
				QueueGpuUpload(gpu_mesh_entity_data, (u32)i, mesh_entity);
			}
			ArrayAppend(gpu_uploads, &alloc, gpu_mesh_entity_data);
		}
		
		for (auto* entity_array : QueryEntities<MeshAssetType>(&alloc, entity_system)) {
			ProfilerScope("MeshAssetTypeGpuComponentUpdate");
			
			u32 dirty_entity_count = (u32)BitArrayCountSetBits(entity_array->dirty_mask);
			if (dirty_entity_count == 0) continue;
			
			auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
			
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				u32 data_size = streams.runtime_data_layout[i].AllocationSize();
				
				u32 allocation_size = AlignUp(data_size, 256u);
				u32 allocation_offset = mesh_asset_buffer_offset;
				
				mesh_asset_buffer_offset += allocation_size;
				streams.allocation[i].base_offset = allocation_offset;
				
				u64 guid = streams.runtime_data_layout[i].file_guid;
				auto file = SystemOpenFile(&alloc, StringFormat(&alloc, "./Assets/Runtime/%x..mrd"_sl, guid), OpenFileFlags::Read);
				
				streams.runtime_file[i].file = file;
				
				auto* cpu_address = mesh_asset_buffer_cpu_address + streams.allocation[i].base_offset;
				SystemReadFile(file, cpu_address, data_size, 0);
			}
			
			auto gpu_mesh_asset_data = AllocateGpuComponentUploadBuffer(&record_context, dirty_entity_count, streams.gpu_mesh_asset_data);
			
			u32* stream_index_to_entity_id = entity_array->stream_index_to_entity_id;
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				auto layout = streams.runtime_data_layout[i];
				u32 base_offset = streams.allocation[i].base_offset;
				
				GpuMeshAssetData mesh_asset;
				mesh_asset.vertex_buffer_offset  = base_offset + layout.VertexBufferOffset();
				mesh_asset.meshlet_buffer_offset = base_offset + layout.MeshletBufferOffset();
				mesh_asset.index_buffer_offset   = base_offset + layout.IndexBufferOffset();
				mesh_asset.meshlet_count         = layout.meshlet_count;
				QueueGpuUpload(gpu_mesh_asset_data, stream_index_to_entity_id[i], mesh_asset);
			}
			ArrayAppend(gpu_uploads, &alloc, gpu_mesh_asset_data);
		}
		
		
		auto renderer_world = world_entity.renderer_world;
		renderer_world->window_size = float2(window_size);
		renderer_world->gpu_uploads = gpu_uploads;
		
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
	ReleaseBufferResource(graphics_context, resource_table.virtual_resources[(u32)VirtualResourceID::GpuMeshEntityData].buffer.resource);
	ReleaseBufferResource(graphics_context, resource_table.virtual_resources[(u32)VirtualResourceID::GpuMeshAssetData].buffer.resource);
	ReleaseBufferResource(graphics_context, mesh_asset_buffer);
	
	for (auto& buffer : upload_buffers) {
		ReleaseBufferResource(graphics_context, buffer);
	}
	
	return 0;
}
