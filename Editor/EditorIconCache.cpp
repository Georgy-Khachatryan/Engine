#include "Basic/Basic.h"
#include "Engine/Entities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "LevelEditor.h"
#include "Renderer/Renderer.h"

enum struct EditorIconState : u32 {
	Free   = 0,
	Wait   = 1,
	Render = 2,
	Ready  = 3,
};

struct EditorIconEntry {
	u64 guid = 0;
	EntityTypeID entity_type_id;
	EditorIconState state = EditorIconState::Free;
	u32 cache_frame_index = 0;
};

compile_const u32 icon_size_pixels = 128u;
compile_const u32 icon_cache_size_icons = 8u;
compile_const u32 icon_cache_area_icons = icon_cache_size_icons * icon_cache_size_icons;
compile_const u32 icon_cache_size_pixels = icon_cache_size_icons * icon_size_pixels;
compile_const u32 icon_render_frame_count = 32u; // Enough to stream in the assets.

struct EditorIconCache {
	WorldEntitySystem world_system;
	VirtualResourceTable* resource_table = nullptr;
	
	NativeTextureResource icon_atlas_resource;
	u32 icon_atlas_texture_id = 0;
	TextureSize icon_atlas_size;
	
	u64 world_entity_guid = 0;
	u64 mesh_entity_guid  = 0;
	
	
	HashTable<u64, u32> icon_guid_to_index;
	Array<u32> free_icon_indices;
	Array<EditorIconEntry> icons;
	u32 cache_frame_index = 0;
	
	
	u32 currently_rendering_icon_index = u32_max;
	u32 currently_rendering_frame_index = 0;
	u32 deallocate_icon_count = 0;
};

EditorIconCache* CreateEditorIconCache(StackAllocator* alloc, GraphicsContext* graphics_context) {
	auto* icon_cache = NewFromAlloc(alloc, EditorIconCache);
	icon_cache->resource_table = CreateResourceTable(alloc);
	InitializeEntitySystem(icon_cache->world_system, alloc);
	
	auto& world_system = icon_cache->world_system;
	
	icon_cache->world_entity_guid = GenerateRandomNumber64(world_system.guid_random_seed);
	
	auto world_entity  = CreateEntity<WorldEntityType>(world_system, icon_cache->world_entity_guid);
	auto camera_entity = CreateEntity<CameraEntityType>(world_system);
	auto mesh_entity   = CreateEntity<MeshEntityType>(world_system);
	auto global_light_entity = CreateEntity<LightEntityType>(world_system);
	
	icon_cache->mesh_entity_guid = mesh_entity.guid->guid;
	
	camera_entity.rotation->rotation =
		Math::AxisAngleToQuat(float3(0.f, 0.f, 1.f), +135.f * Math::degrees_to_radians) *
		Math::AxisAngleToQuat(float3(1.f, 0.f, 0.f), -135.f * Math::degrees_to_radians);
	camera_entity.camera->vertical_fov_degrees = 45.f;
	
	global_light_entity.light->type = LightType::Global;
	global_light_entity.light->irradiance = 10.f;
	global_light_entity.rotation->rotation = Math::AxisAxisZToQuat(Math::Normalize(float3(-1.f, 0.f, 1.f)));
	
	world_entity.camera_entity->guid       = camera_entity.guid->guid;
	world_entity.global_light_entity->guid = global_light_entity.guid->guid;
	world_entity.tone_mapping_settings->method  = ToneMappingMethod::GT7_SDR;
	world_entity.anti_aliasing_settings->method = AntiAliasingMethod::None;
	
	icon_cache->icon_atlas_size = TextureSize(TextureFormat::R8G8B8A8_UNORM_SRGB, icon_cache_size_pixels, icon_cache_size_pixels);
	icon_cache->icon_atlas_resource = CreateTextureResource(graphics_context, icon_cache->icon_atlas_size, CreateResourceFlags::UAV);
	icon_cache->icon_atlas_texture_id = AllocatePersistentSrvDescriptor(graphics_context);
	
	ArrayReserve(icon_cache->free_icon_indices, alloc, icon_cache_area_icons);
	for (u32 i = 0; i < icon_cache_area_icons; i += 1) {
		ArrayAppend(icon_cache->free_icon_indices, icon_cache_area_icons - i - 1);
	}
	
	HashTableReserve(icon_cache->icon_guid_to_index, alloc, icon_cache_area_icons);
	ArrayResize(icon_cache->icons, alloc, icon_cache_area_icons);
	
	return icon_cache;
}

void ReleaseEditorIconCache(EditorIconCache* icon_cache, GraphicsContext* graphics_context) {
	DeallocatePersistentSrvDescriptor(graphics_context, icon_cache->icon_atlas_texture_id);
	ReleaseTextureResource(graphics_context, icon_cache->icon_atlas_resource);
	ReleaseEntitySystemGpuStreamAllocations(graphics_context, icon_cache->world_system);
	ReleaseHeapAllocator(icon_cache->world_system.heap);
	ReleaseResourceTable(graphics_context, icon_cache->resource_table);
}


void EditorIconCacheDrawMeshIcon(EditorIconCache* icon_cache, u64 mesh_asset_guid) {
	auto* element = HashTableFind(icon_cache->icon_guid_to_index, mesh_asset_guid);
	if (element == nullptr && icon_cache->icon_guid_to_index.count < icon_cache_area_icons) {
		auto [new_element, is_inserted] = HashTableAddOrFind(icon_cache->icon_guid_to_index, mesh_asset_guid, ArrayPopLast(icon_cache->free_icon_indices));
		DebugAssert(is_inserted, "Failed to add icon entry.");
		element = new_element;
		
		auto& icon = icon_cache->icons[element->value];
		icon.guid           = mesh_asset_guid;
		icon.entity_type_id = {}; // TODO: Different entity type icons.
		icon.state          = EditorIconState::Wait;
	}
	
	if (element != nullptr) {
		icon_cache->icons[element->value].cache_frame_index = icon_cache->cache_frame_index;
	} else {
		icon_cache->deallocate_icon_count += 1;
	}
	
	if (element != nullptr && icon_cache->icons[element->value].state == EditorIconState::Ready) {
		u32 icon_index = element->value;
		
		auto icon_coordinates = float2((float)(icon_index % icon_cache_size_icons), (float)(icon_index / icon_cache_size_icons));
		auto uv_min = icon_coordinates * (1.f / icon_cache_size_icons);
		auto uv_max = uv_min + (1.f / icon_cache_size_icons);
		
		ImGui::ImageButtonEx("AssetIcon", icon_cache->icon_atlas_texture_id, ImVec2(icon_size_pixels, icon_size_pixels), ImGuiButtonFlags_None, uv_min, uv_max);
	} else {
		float4 color = float4(Math::DecodeR10G10B10((u32)mesh_asset_guid), 1.f);
		ImGui::ColorButton("AssetIcon", color, ImGuiColorEditFlags_None, ImVec2(icon_size_pixels, icon_size_pixels));
	}
}

void EditorIconCacheUpdate(StackAllocator* alloc, EditorIconCache* icon_cache, GraphicsContext* graphics_context, AssetEntitySystem& asset_system, Array<EditorWorldView>& editor_world_views) {
	auto& world_system = icon_cache->world_system;
	
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, icon_cache->world_entity_guid);
	auto camera_entity = QueryEntityByGUID<CameraEntityType>(world_system, world_entity.camera_entity->guid);
	
	ImGuiDrawList3D draw_list_3d;
	draw_list_3d.alloc = alloc;
	
	
	if (icon_cache->deallocate_icon_count != 0) {
		TempAllocationScope(alloc);
		
		Array<u64> deallocation_candidates;
		ArrayReserve(deallocation_candidates, alloc, icon_cache_area_icons);
		
		for (u32 icon_index = 0; icon_index < icon_cache_area_icons; icon_index += 1) {
			auto& icon = icon_cache->icons[icon_index];
			if (icon.cache_frame_index == icon_cache->cache_frame_index || icon.state == EditorIconState::Render || icon.state == EditorIconState::Free) continue;
			
			ArrayAppend(deallocation_candidates, icon_index | (u64)icon.cache_frame_index << 32);
		}
		
		HeapSort<u64>(deallocation_candidates);
		
		deallocation_candidates.count = Math::Min(deallocation_candidates.count, (u64)icon_cache->deallocate_icon_count);
		
		for (u64 candidate : deallocation_candidates) {
			u32 icon_index = (u32)candidate;
			HashTableRemove(icon_cache->icon_guid_to_index, icon_cache->icons[icon_index].guid);
			icon_cache->icons[icon_index] = {};
			ArrayAppend(icon_cache->free_icon_indices, icon_index);
		}
		icon_cache->deallocate_icon_count = 0;
	}
	
	
	u32 rendering_icon_index = icon_cache->currently_rendering_icon_index;
	if (rendering_icon_index != u32_max && icon_cache->currently_rendering_frame_index >= icon_render_frame_count) {
		icon_cache->currently_rendering_frame_index = 0;
		icon_cache->icons[rendering_icon_index].state = EditorIconState::Ready;
		rendering_icon_index = u32_max;
	}
	
	
	if (rendering_icon_index == u32_max) {
		for (u32 icon_index = 0; icon_index < icon_cache_area_icons; icon_index += 1) {
			auto& icon = icon_cache->icons[icon_index];
			if (icon.state == EditorIconState::Wait) {
				icon.state = EditorIconState::Render;
				rendering_icon_index = icon_index;
				break;
			}
		}
		icon_cache->currently_rendering_icon_index = rendering_icon_index;
	}
	
	
	u64 mesh_asset_guid = rendering_icon_index != u32_max ? icon_cache->icons[rendering_icon_index].guid : 0;
	
	{
		auto mesh_entity_id = FindEntityByGUID(world_system, icon_cache->mesh_entity_guid);
		auto mesh_entity = ExtractComponentStreams<MeshEntityType>(&world_system.entity_type_arrays[mesh_entity_id.entity_type_id.index], mesh_entity_id.entity_id);
		
		if (mesh_entity.mesh_asset->guid != mesh_asset_guid) {
			mesh_entity.mesh_asset->guid = mesh_asset_guid;
			
			BitArraySetBit(world_system.entity_type_arrays[mesh_entity_id.entity_type_id.index].dirty_mask, mesh_entity_id.entity_id.index);
		}
	}
	
	//
	// Position the camera such that the mesh AABB corners are within the frustum.
	// Using convex hull or k-dop vertices could result in an even a better fit.
	//
	if (mesh_asset_guid != 0) {
		auto mesh_asset = QueryEntityByGUID<MeshAssetType>(asset_system, mesh_asset_guid);
		
		auto& aabb   = *mesh_asset.aabb;
		auto& camera = *camera_entity.camera;
		
		// Scale FOV to have some padding around the edges.
		float inner_fov_scale = 0.95f;
		float4 view_to_clip_coef = Math::PerspectiveViewToClip(camera.vertical_fov_degrees * Math::degrees_to_radians * inner_fov_scale, icon_size_pixels, camera.near_depth);
		
		auto view_to_world_rotation = Math::QuatToRotationMatrix(camera_entity.rotation->rotation);
		auto world_to_view_rotation = Math::Transpose(view_to_world_rotation);
		
		float3 planes[4];
		planes[0] = float3(-view_to_clip_coef.x, 0.f, -1.f);
		planes[1] = float3(+view_to_clip_coef.x, 0.f, -1.f);
		planes[2] = float3(0.f, +view_to_clip_coef.y, -1.f);
		planes[3] = float3(0.f, -view_to_clip_coef.y, -1.f);
		
		// Find distances to each frustum plane.
		float4 distance = -FLT_MAX;
		for (u32 i = 0; i < 8; i += 1) {
			float3 world_space_position = Math::Lerp(aabb.min, aabb.max, float3(uint3(i, i >> 1, i >> 2) & 0x1));
			float3 view_space_position  = world_to_view_rotation * world_space_position;
			
			for (u32 plane_index = 0; plane_index < 4; plane_index += 1) {
				distance[plane_index] = Math::Max(distance[plane_index], Math::Dot(view_space_position, planes[plane_index]));
			}
			
			// float radius = Math::Length(aabb.max - aabb.min) * 0.5f;
			// draw_list_3d.AddSphere(world_space_position, radius * 0.01f, u32_max);
		}
		
		float px = (distance[0] - distance[1]) / (2.f * planes[0].x);
		float py = (distance[2] - distance[3]) / (2.f * planes[2].y);
		
		float pz0 = (distance[0] + distance[1]) / (2.f * planes[0].z);
		float pz1 = (distance[2] + distance[3]) / (2.f * planes[2].z);
		
		float3 view_space_camera_position = float3(px, py, Math::Min(pz0, pz1));
		camera_entity.position->position = view_to_world_rotation * view_space_camera_position;
	}
	
	if (mesh_asset_guid != 0) {
		auto renderer_world = world_entity.renderer_world;
		renderer_world->window_size                  = float2(icon_size_pixels, icon_size_pixels);
		renderer_world->delta_time                   = ImGui::GetIO().DeltaTime;
		renderer_world->debug_mesh_instance_arrays   = draw_list_3d.Flush();
		renderer_world->reference_path_tracer_percent = 1.f;
		
		auto& output_settings = renderer_world->output_settings;
		output_settings.mode = SceneOutputMode::ExternalRenderTarget;
		output_settings.descriptor_index       = icon_cache->icon_atlas_texture_id;
		output_settings.external.output_offset = uint2(rendering_icon_index % icon_cache_size_icons, rendering_icon_index / icon_cache_size_icons) * icon_size_pixels;
		output_settings.external.resource      = icon_cache->icon_atlas_resource;
		output_settings.external.size          = icon_cache->icon_atlas_size;
		
		auto& world_view = ArrayEmplace(editor_world_views, alloc);
		world_view.resource_table    = icon_cache->resource_table;
		world_view.world_system      = &world_system;
		world_view.world_entity_guid = icon_cache->world_entity_guid;
		
		icon_cache->currently_rendering_frame_index += 1;
	}
	
	icon_cache->cache_frame_index += 1;
}
