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
	Rendering   = 3,
	Ready       = 4,
};

struct EditorIconEntry {
	u64 guid = 0;
	u64 dependency_mask = 0; // Bloom filter of all dependencies of this icon.
	
	EntityTypeID entity_type_id;
	EditorIconState state = EditorIconState::Free;
	
	u32 cache_frame_index    = 0;
	u32 rendered_frame_count = 0;
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


static u64 DependencyMaskFromGUID(u64 guid) {
	return guid ? 1ull << (guid & 0x3F) : 0;
}

static bool RequestAssetStreaming(AssetEntitySystem& asset_system, u64 guid) {
	if (guid == 0) return true;
	
	auto query = QueryEntityByGUID<AssetCpuStreamingRequestQuery>(asset_system, guid);
	bool result = true;
	
	if (query.mesh_cpu_streaming_requests) {
		result &= query.mesh_cpu_streaming_requests->RequestMinimumResidency();
	}
	
	if (query.texture_cpu_streaming_requests) {
		result &= query.texture_cpu_streaming_requests->RequestMinimumResidency(icon_size_pixels);
	}
	
	return result;
}

struct IconRenderingAssets {
	MeshAssetGUID mesh_asset_guid;
	FixedCapacityArray<MaterialAssetGUID, MeshAssetMaterialTable::max_materials> materials;
	FixedCapacityArray<TextureAssetGUID, MeshAssetMaterialTable::max_materials * 4u> textures;
};

static IconRenderingAssets GatherIconRenderingAssets(AssetEntitySystem& asset_system, EditorIconCache* icon_cache, const EditorIconEntry& icon) {
	IconRenderingAssets assets;
	
	if (icon.entity_type_id.index == ECS::GetEntityTypeID<MeshAssetType>::id.index) {
		assets.mesh_asset_guid = { icon.guid };
	} else if (icon.entity_type_id.index == ECS::GetEntityTypeID<MaterialAssetType>::id.index) {
		assets.mesh_asset_guid = { icon_cache->material_icon_mesh_guid };
	}
	
	if (assets.mesh_asset_guid.guid != 0) {
		auto mesh_asset = QueryEntityByGUID<MeshAssetType>(asset_system, assets.mesh_asset_guid.guid);
		assets.materials = mesh_asset.material_table->materials;
	}
	
	if (icon.entity_type_id.index == ECS::GetEntityTypeID<MaterialAssetType>::id.index) {
		for (auto& material_asset_guid : assets.materials) {
			if (material_asset_guid.guid != 0) continue;
			
			material_asset_guid.guid = icon.guid;
		}
	}
	
	auto material_asset_streams = ExtractComponentStreams<MaterialAssetType>(QueryEntityTypeArray<MaterialAssetType>(asset_system));
	for (auto& material_asset_guid : assets.materials) {
		if (material_asset_guid.guid == 0) continue;
		
		auto typed_entity_id = FindEntityByGUID(asset_system, material_asset_guid.guid);
		
		auto& texture_data = material_asset_streams.texture_data[typed_entity_id.entity_id.index];
		ArrayAppend(assets.textures, texture_data.albedo);
		ArrayAppend(assets.textures, texture_data.normal);
		ArrayAppend(assets.textures, texture_data.roughness);
		ArrayAppend(assets.textures, texture_data.metalness);
	}
	
	return assets;
}

static void DrawDefaultEntityTypeIcon(EntityTypeID entity_type_id) {
	float4 color = float4(Math::DecodeR10G10B10((u32)ComputeHash64(entity_type_id.index)), 1.f);
	ImGui::ColorButton("AssetIcon", color, ImGuiColorEditFlags_None, ImVec2(icon_size_pixels, icon_size_pixels));
}

void EditorIconCacheDrawIcon(EditorIconCache* icon_cache, EntitySystemBase& entity_system, u64 entity_guid, EntityTypeID entity_type_id) {
	if (entity_type_id.index == ECS::GetEntityTypeID<TextureAssetType>::id.index) {
		auto texture_asset = QueryEntityByGUID<TextureAssetType>(entity_system, entity_guid);
		if (texture_asset.cpu_streaming_requests->RequestMinimumResidency(icon_size_pixels)) {
			auto texture_id = ImGuiTextureID(texture_asset.descriptor_allocation->index, texture_asset.runtime_data_layout->size.type);
			ImGui::ImageButtonEx("AssetIcon", texture_id, ImVec2(icon_size_pixels, icon_size_pixels));
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
	if (icon_state == EditorIconState::Ready || icon_state == EditorIconState::Rendering || icon_state == EditorIconState::Invalidated) {
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
		if (icon.state == EditorIconState::Free || icon.state == EditorIconState::Ready) continue;
		
		auto assets = GatherIconRenderingAssets(asset_system, icon_cache, icon);
		
		bool is_ready = true;
		is_ready &= RequestAssetStreaming(asset_system, assets.mesh_asset_guid.guid);
		for (auto texture_asset_guid : assets.textures) {
			is_ready &= RequestAssetStreaming(asset_system, texture_asset_guid.guid);
		}
		
		if (icon.state == EditorIconState::Rendering) {
			icon.rendered_frame_count += 1;
			
			if (icon.rendered_frame_count >= number_of_frames_in_flight) {
				icon.state = EditorIconState::Ready;
			}
		}
		
		if ((icon.state == EditorIconState::Wait || icon.state == EditorIconState::Invalidated) && is_ready && rendering_icon_index == u32_max) {
			icon.state = EditorIconState::Rendering;
			icon.rendered_frame_count = 0;
			rendering_icon_index = icon_index;
		}
	}
	
	
	if (rendering_icon_index != u32_max) {
		auto& icon = icon_cache->icons[rendering_icon_index];
		
		auto mesh_entity = QueryEntityByGUID<MeshEntityType>(world_system, icon_cache->mesh_entity_guid);
		BitArraySetBit(mesh_entity.array->dirty_mask, mesh_entity.entity_id.index);
		
		auto assets = GatherIconRenderingAssets(asset_system, icon_cache, icon);
		
		*mesh_entity.mesh_asset = assets.mesh_asset_guid;
		for (u64 i = 0; i < assets.materials.count; i += 1) {
			mesh_entity.material_table->materials[i] = assets.materials[i];
		}
		
		u64 dependency_mask = DependencyMaskFromGUID(assets.mesh_asset_guid.guid);
		for (auto material_asset_guid : assets.materials) {
			dependency_mask |= DependencyMaskFromGUID(material_asset_guid.guid);
		}
		for (auto texture_asset_guid : assets.textures) {
			dependency_mask |= DependencyMaskFromGUID(texture_asset_guid.guid);
		}
		icon.dependency_mask = dependency_mask;
		
		
		//
		// Position the camera such that the mesh AABB corners are within the frustum.
		// Using convex hull or k-dop vertices could result in an even a better fit.
		//
		if (assets.mesh_asset_guid.guid != 0) {
			auto mesh_asset = QueryEntityByGUID<MeshAssetType>(asset_system, assets.mesh_asset_guid.guid);
			
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
		renderer_world->window_size                  = float2(icon_size_pixels, icon_size_pixels) * 4.f;
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
	}
	
	
	// Invalidate icons after we select an icon to render this frame. Otherwise when recreating icons we might select and draw it before it's streamed in.
	{
		FixedCapacityArray<EntityTypeArray*, 3> entity_type_arrays;
		ArrayAppend(entity_type_arrays, QueryEntityTypeArray<MaterialAssetType>(asset_system));
		ArrayAppend(entity_type_arrays, QueryEntityTypeArray<TextureAssetType>(asset_system));
		ArrayAppend(entity_type_arrays, QueryEntityTypeArray<MeshAssetType>(asset_system));
		
		u64 dependency_mask = 0;
		for (auto* entity_array : entity_type_arrays) {
			auto streams = ExtractComponentStreams<GuidQuery>(entity_array);
			
			for (u64 i : BitArrayIt(entity_array->dirty_mask)) {
				dependency_mask |= DependencyMaskFromGUID(streams.guid[i].guid);
			}
			
			for (u64 i : BitArrayIt(entity_array->created_mask)) {
				dependency_mask |= DependencyMaskFromGUID(streams.guid[i].guid);
			}
		}
		
		// Invalidate material icons when material preview mesh is changed.
		bool invalidate_materials = false;
		if (icon_cache->material_icon_mesh_guid != editor_settings_entity.icon_cache_settings->material_icon_mesh.guid) {
			icon_cache->material_icon_mesh_guid = editor_settings_entity.icon_cache_settings->material_icon_mesh.guid;
			invalidate_materials = true;
		}
		
		// Invalidate dirty or recreated icons:
		if (dependency_mask != 0 || invalidate_materials) {
			for (u32 icon_index = 0; icon_index < icon_cache_area_icons; icon_index += 1) {
				auto& icon = icon_cache->icons[icon_index];
				if ((icon.dependency_mask & dependency_mask) == 0 && (invalidate_materials == false || icon.entity_type_id.index != ECS::GetEntityTypeID<MaterialAssetType>::id.index)) continue;
				
				if (icon.state == EditorIconState::Rendering || icon.state == EditorIconState::Ready) {
					icon.state = EditorIconState::Invalidated;
				}
			}
		}
	}
	
	icon_cache->cache_frame_index += 1;
}
