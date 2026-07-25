#include "Basic/Basic.h"
#include "Engine/Entities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "LevelEditor.h"
#include "Renderer/Renderer.h"

struct EditorIconCache {
	WorldEntitySystem world_system;
	VirtualResourceTable* resource_table = nullptr;
	
	u64 world_entity_guid = 0;
	u64 mesh_entity_guid  = 0;
	
	u64 mesh_asset_guid = 0;
	u32 scene_descriptor_heap_offset = 0;
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
	camera_entity.camera->vertical_fov_degrees = 70.f;
	
	global_light_entity.light->type = LightType::Global;
	global_light_entity.light->irradiance = 10.f;
	global_light_entity.rotation->rotation = Math::AxisAxisZToQuat(Math::Normalize(float3(-1.f, 0.f, 1.f)));
	
	world_entity.camera_entity->guid       = camera_entity.guid->guid;
	world_entity.global_light_entity->guid = global_light_entity.guid->guid;
	world_entity.tone_mapping_settings->method  = ToneMappingMethod::GT7_SDR;
	world_entity.anti_aliasing_settings->method = AntiAliasingMethod::None;
	
	return icon_cache;
}

void ReleaseEditorIconCache(EditorIconCache* icon_cache, GraphicsContext* graphics_context) {
	ReleaseEntitySystemGpuStreamAllocations(graphics_context, icon_cache->world_system);
	ReleaseHeapAllocator(icon_cache->world_system.heap);
	ReleaseResourceTable(graphics_context, icon_cache->resource_table);
}


u32 EditorIconCacheQueryMeshIcon(EditorIconCache* icon_cache, u64 mesh_asset_guid) {
	icon_cache->mesh_asset_guid = mesh_asset_guid;
	return icon_cache->scene_descriptor_heap_offset;
}

void EditorIconCacheBegin(EditorIconCache* icon_cache, GraphicsContext* graphics_context) {
	icon_cache->mesh_asset_guid = 0;
	icon_cache->scene_descriptor_heap_offset = AllocateTransientSrvDescriptorTable(graphics_context, 1);
}

void EditorIconCacheEnd(StackAllocator* alloc, EditorIconCache* icon_cache, GraphicsContext* graphics_context, AssetEntitySystem& asset_system, Array<EditorWorldView>& editor_world_views) {
	auto& world_system = icon_cache->world_system;
	
	auto world_entity = QueryEntityByGUID<WorldEntityType>(world_system, icon_cache->world_entity_guid);
	auto camera_entity = QueryEntityByGUID<CameraEntityType>(world_system, world_entity.camera_entity->guid);
	
	u32 scene_descriptor_heap_offset = icon_cache->scene_descriptor_heap_offset;
	
	ImGuiDrawList3D draw_list_3d;
	draw_list_3d.alloc = alloc;
	
	u64 mesh_asset_guid = icon_cache->mesh_asset_guid;
	
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
		float4 view_to_clip_coef = Math::PerspectiveViewToClip(camera.vertical_fov_degrees * Math::degrees_to_radians * inner_fov_scale, 128.f, camera.near_depth);
		
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
		renderer_world->window_size                  = float2(128.f, 128.f);
		renderer_world->delta_time                   = ImGui::GetIO().DeltaTime;
		renderer_world->scene_descriptor_heap_offset = scene_descriptor_heap_offset;
		renderer_world->debug_mesh_instance_arrays   = draw_list_3d.Flush();
		renderer_world->reference_path_tracer_percent = 1.f;
		
		auto& world_view = ArrayEmplace(editor_world_views, alloc);
		world_view.resource_table    = icon_cache->resource_table;
		world_view.world_system      = &world_system;
		world_view.world_entity_guid = icon_cache->world_entity_guid;
	}
}
