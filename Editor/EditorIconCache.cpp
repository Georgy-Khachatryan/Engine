#include "Basic/Basic.h"
#include "EditorEntities.h"
#include "Engine/Entities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "LevelEditor.h"
#include "Renderer/Renderer.h"

enum struct EditorIconState : u32 {
	Free        = 0,
	Wait        = 1,
	Invalidated = 2,
	Ready       = 3,
};

struct EditorIconEntry {
	u64 guid = 0;
	u64 dependencies_mask = 0; // Bloom filter of all dependencies of this icon.
	
	EntityTypeID entity_type_id;
	EditorIconState state = EditorIconState::Free;
	u32 cache_frame_index = 0;
};

compile_const u32 icon_size_pixels        = 128u;
compile_const u32 icon_cache_size_icons   = 8u;
compile_const u32 icon_cache_area_icons   = icon_cache_size_icons * icon_cache_size_icons;
compile_const u32 icon_cache_size_pixels  = icon_cache_size_icons * icon_size_pixels;

struct EditorIconCache {
	WorldEntitySystem world_system;
	VirtualResourceTable* resource_table = nullptr;
	
	NativeTextureResource icon_atlas_resource;
	u32 icon_atlas_texture_id = 0;
	TextureSize icon_atlas_size;
	
	u64 world_entity_guid = 0;
	u64 mesh_entity_guid  = 0;
	u64 material_icon_mesh_guid = 0;
	
	
	HashTable<u64, u32> icon_guid_to_index;
	Array<u32> free_icon_indices;
	Array<EditorIconEntry> icons;
	u32 cache_frame_index = 0;
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


// TODO: Implement a generic way to iterate over referenced entities.
static u64 DependenciesFromGUID(u64 guid) {
	return 1ull << (guid & 0x3F);
}

static u64 GatherTextureAssetDependencies(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return 0;
	
	return DependenciesFromGUID(guid);
}

static u64 GatherMaterialAssetDependencies(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return 0;
	
	auto material_asset = QueryEntityByGUID<MaterialAssetType>(asset_system, guid);
	
	u64 dependencies_mask = DependenciesFromGUID(guid);
	dependencies_mask |= GatherTextureAssetDependencies(asset_system, material_asset.texture_data->albedo.guid);
	dependencies_mask |= GatherTextureAssetDependencies(asset_system, material_asset.texture_data->normal.guid);
	dependencies_mask |= GatherTextureAssetDependencies(asset_system, material_asset.texture_data->roughness.guid);
	dependencies_mask |= GatherTextureAssetDependencies(asset_system, material_asset.texture_data->metalness.guid);
	
	return dependencies_mask;
}

static u64 GatherMeshAssetDependencies(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return 0;
	
	return DependenciesFromGUID(guid);
}


static bool RequestTextureAssetStreaming(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return true;
	
	auto texture_asset = QueryEntityByGUID<TextureAssetType>(asset_system, guid);
	return texture_asset.cpu_streaming_requests->RequestMinimumResidency(icon_size_pixels);
}

static bool RequestMaterialAssetStreaming(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return true;
	
	auto material_asset = QueryEntityByGUID<MaterialAssetType>(asset_system, guid);
	
	bool is_ready = true;
	is_ready &= RequestTextureAssetStreaming(asset_system, material_asset.texture_data->albedo.guid);
	is_ready &= RequestTextureAssetStreaming(asset_system, material_asset.texture_data->normal.guid);
	is_ready &= RequestTextureAssetStreaming(asset_system, material_asset.texture_data->roughness.guid);
	is_ready &= RequestTextureAssetStreaming(asset_system, material_asset.texture_data->metalness.guid);
	
	return is_ready;
}

static bool RequestMeshAssetStreaming(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return true;
	
	auto mesh_asset = QueryEntityByGUID<MeshAssetType>(asset_system, guid);
	return mesh_asset.cpu_streaming_requests->RequestMinimumResidency();
}


static void DrawDefaultEntityTypeIcon(EntityTypeID entity_type_id) {
	float4 color = float4(Math::DecodeR10G10B10((u32)ComputeHash64(entity_type_id.index)), 1.f);
	ImGui::ColorButton("AssetIcon", color, ImGuiColorEditFlags_None, ImVec2(icon_size_pixels, icon_size_pixels));
}

void EditorIconCacheDrawIcon(EditorIconCache* icon_cache, EntitySystemBase& entity_system, u64 entity_guid, EntityTypeID entity_type_id) {
	if (entity_type_id.index == ECS::GetEntityTypeID<TextureAssetType>::id.index) {
		auto texture_asset = QueryEntityByGUID<TextureAssetType>(entity_system, entity_guid);
		if (texture_asset.cpu_streaming_requests->RequestMinimumResidency(icon_size_pixels)) {
			ImGui::ImageButtonEx("AssetIcon", texture_asset.descriptor_allocation->index, ImVec2(icon_size_pixels, icon_size_pixels));
		} else {
			DrawDefaultEntityTypeIcon(entity_type_id);
		}
		return;
	}
	
	bool is_supported_entity_type =
		(entity_type_id.index == ECS::GetEntityTypeID<MeshAssetType>::id.index) ||
		(entity_type_id.index == ECS::GetEntityTypeID<MaterialAssetType>::id.index);
	
	u32 icon_index = u32_max;
	if (is_supported_entity_type) {
		auto* element = HashTableFind(icon_cache->icon_guid_to_index, entity_guid);
		if (element == nullptr && icon_cache->icon_guid_to_index.count < icon_cache_area_icons) {
			auto [new_element, is_inserted] = HashTableAddOrFind(icon_cache->icon_guid_to_index, entity_guid, ArrayPopLast(icon_cache->free_icon_indices));
			DebugAssert(is_inserted, "Failed to add icon entry.");
			element = new_element;
			
			auto& icon = icon_cache->icons[element->value];
			icon.guid           = entity_guid;
			icon.entity_type_id = entity_type_id;
			icon.state          = EditorIconState::Wait;
		}
		
		if (element != nullptr) {
			icon_index = element->value;
			icon_cache->icons[icon_index].cache_frame_index = icon_cache->cache_frame_index;
		} else {
			icon_cache->deallocate_icon_count += 1;
		}
	}
	
	auto icon_state = icon_index != u32_max ? icon_cache->icons[icon_index].state : EditorIconState::Free;
	if (icon_state == EditorIconState::Ready || icon_state == EditorIconState::Invalidated) {
		auto icon_coordinates = float2((float)(icon_index % icon_cache_size_icons), (float)(icon_index / icon_cache_size_icons));
		auto uv_min = icon_coordinates * (1.f / icon_cache_size_icons);
		auto uv_max = uv_min + (1.f / icon_cache_size_icons);
		
		ImGui::ImageButtonEx("AssetIcon", icon_cache->icon_atlas_texture_id, ImVec2(icon_size_pixels, icon_size_pixels), ImGuiButtonFlags_None, uv_min, uv_max);
	} else {
		DrawDefaultEntityTypeIcon(entity_type_id);
	}
}

void EditorIconCacheUpdate(StackAllocator* alloc, EditorIconCache* icon_cache, AssetEntitySystem& asset_system, Array<EditorWorldView>& editor_world_views) {
	auto& world_system = icon_cache->world_system;
	
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, icon_cache->world_entity_guid);
	auto camera_entity = QueryEntityByGUID<CameraEntityType>(world_system, world_entity.camera_entity->guid);
	auto editor_settings_entity = QueryFirstEntityByType<EditorSettingsEntityType>(asset_system);
	
	ImGuiDrawList3D draw_list_3d;
	draw_list_3d.alloc = alloc;
	
	if (icon_cache->deallocate_icon_count != 0) {
		TempAllocationScope(alloc);
		
		Array<u64> deallocation_candidates;
		ArrayReserve(deallocation_candidates, alloc, icon_cache_area_icons);
		
		for (u32 icon_index = 0; icon_index < icon_cache_area_icons; icon_index += 1) {
			auto& icon = icon_cache->icons[icon_index];
			if (icon.cache_frame_index == icon_cache->cache_frame_index || icon.state == EditorIconState::Free) continue;
			
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
	
	
	u32 rendering_icon_index = u32_max;
	for (u32 icon_index = 0; icon_index < icon_cache_area_icons; icon_index += 1) {
		auto& icon = icon_cache->icons[icon_index];
		
		if (icon.state == EditorIconState::Wait || icon.state == EditorIconState::Invalidated) {
			u64 mesh_asset_guid     = 0;
			u64 material_asset_guid = 0;
				
			if (icon.entity_type_id.index == ECS::GetEntityTypeID<MeshAssetType>::id.index) {
				auto mesh_asset = QueryEntityByGUID<MeshAssetType>(asset_system, icon.guid);
				mesh_asset_guid     = icon.guid;
				material_asset_guid = mesh_asset.material_asset->guid;
			} else if (icon.entity_type_id.index == ECS::GetEntityTypeID<MaterialAssetType>::id.index) {
				mesh_asset_guid     = icon_cache->material_icon_mesh_guid;
				material_asset_guid = icon.guid;
			}
			
			bool is_ready = true;
			is_ready &= RequestMeshAssetStreaming(asset_system, mesh_asset_guid);
			is_ready &= RequestMaterialAssetStreaming(asset_system, material_asset_guid);
			
			if (is_ready) {
				icon.state = EditorIconState::Ready;
				rendering_icon_index = icon_index;
				break;
			}
		}
	}
	
	
	if (rendering_icon_index != u32_max) {
		auto& icon = icon_cache->icons[rendering_icon_index];
		
		u64 mesh_asset_guid     = 0;
		u64 material_asset_guid = 0;
		
		if (icon.entity_type_id.index == ECS::GetEntityTypeID<MeshAssetType>::id.index) {
			auto mesh_asset = QueryEntityByGUID<MeshAssetType>(asset_system, icon.guid);
			mesh_asset_guid     = icon.guid;
			material_asset_guid = mesh_asset.material_asset->guid;
		} else if (icon.entity_type_id.index == ECS::GetEntityTypeID<MaterialAssetType>::id.index) {
			mesh_asset_guid     = icon_cache->material_icon_mesh_guid;
			material_asset_guid = icon.guid;
		}
		
		
		auto mesh_entity_id = FindEntityByGUID(world_system, icon_cache->mesh_entity_guid);
		auto mesh_entity = ExtractComponentStreams<MeshEntityType>(&world_system.entity_type_arrays[mesh_entity_id.entity_type_id.index], mesh_entity_id.entity_id);
		BitArraySetBit(world_system.entity_type_arrays[mesh_entity_id.entity_type_id.index].dirty_mask, mesh_entity_id.entity_id.index);
		
		mesh_entity.mesh_asset->guid     = mesh_asset_guid;
		mesh_entity.material_asset->guid = material_asset_guid;
		
		icon.dependencies_mask = GatherMeshAssetDependencies(asset_system, mesh_asset_guid) | GatherMaterialAssetDependencies(asset_system, material_asset_guid);
		
		
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
	}
	
	
	if (rendering_icon_index != u32_max) {
		auto renderer_world = world_entity.renderer_world;
		renderer_world->window_size                  = float2(icon_size_pixels, icon_size_pixels);
		renderer_world->delta_time                   = ImGui::GetIO().DeltaTime;
		renderer_world->debug_mesh_instance_arrays   = draw_list_3d.Flush();
		renderer_world->reference_path_tracer_mode   = ReferencePathTracerMode::WavePerPixel;
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
	}
	
	
	// Invalidate icons after we select an icon to render this frame. Otherwise when recreating icons we might select and draw it before it's streamed in.
	{
		FixedCapacityArray<EntityTypeArray*, 3> entity_type_arrays;
		ArrayAppend(entity_type_arrays, QueryEntityTypeArray<MaterialAssetType>(asset_system));
		ArrayAppend(entity_type_arrays, QueryEntityTypeArray<TextureAssetType>(asset_system));
		ArrayAppend(entity_type_arrays, QueryEntityTypeArray<MeshAssetType>(asset_system));
		
		u64 dirty_dependencies_mask = 0;
		for (auto* entity_array : entity_type_arrays) {
			auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
			
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				dirty_dependencies_mask |= DependenciesFromGUID(streams.guid[i].guid);
			}
			
			for (u64 i : BitArrayIt(entity_array->created_mask)) {
				dirty_dependencies_mask |= DependenciesFromGUID(streams.guid[i].guid);
			}
		}
		
		// Invalidate material icons when material preview mesh is changed.
		bool invalidate_materials = false;
		if (icon_cache->material_icon_mesh_guid != editor_settings_entity.icon_cache_settings->material_icon_mesh.guid) {
			icon_cache->material_icon_mesh_guid = editor_settings_entity.icon_cache_settings->material_icon_mesh.guid;
			invalidate_materials = true;
		}
		
		// Invalidate dirty or recreated icons:
		if (dirty_dependencies_mask != 0 || invalidate_materials) {
			for (u32 icon_index = 0; icon_index < icon_cache_area_icons; icon_index += 1) {
				auto& icon = icon_cache->icons[icon_index];
				if ((icon.dependencies_mask & dirty_dependencies_mask) == 0 && (invalidate_materials == false || icon.entity_type_id.index != ECS::GetEntityTypeID<MaterialAssetType>::id.index)) continue;
				
				icon.state = EditorIconState::Invalidated;
			}
		}
	}
	
	icon_cache->cache_frame_index += 1;
}
