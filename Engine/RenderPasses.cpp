#include "Basic/Basic.h"
#include "GraphicsApi/RecordContext.h"
#include "GraphicsApi/GraphicsApi.h"
#include "RenderPasses.h"
#include "EntitySystem.h"
#include "Entities.h"

static void BuildResourceTable(RecordContext* record_context, EntitySystem* entity_system, RendererWorld* renderer_world, uint2 render_target_size) {
	using ID = VirtualResourceID;
	auto& table = *record_context->resource_table;
	
	table.virtual_resources.count = (u64)ID::Count;
	table.Set(ID::TransmittanceLut,      TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::transmittance_lut_size));
	table.Set(ID::MultipleScatteringLut, TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::multiple_scattering_lut_size));
	table.Set(ID::SkyPanoramaLut,        TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::sky_panorama_lut_size));
	
	auto* mesh_entities = QueryEntityTypeArray<MeshEntityType>(*entity_system);
	table.Set(ID::VisibleMeshlets, (u32)(renderer_world->meshlets.count * mesh_entities->count * sizeof(uint2)));
	table.Set(ID::MeshletIndirectArguments, sizeof(uint4));
	
	table.Set(ID::SceneRadiance, TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size));
	table.Set(ID::DepthStencil,  TextureSize(TextureFormat::D32_FLOAT, render_target_size));
}

void BuildRenderPassesForFrame(RecordContext* record_context, EntitySystem* entity_system, u64 world_entity_guid) {
	auto world_entity = QueryEntityByGUID<WorldEntityType>(*entity_system, world_entity_guid);
	auto camera_entity = QueryEntityByGUID<CameraEntityType>(*entity_system, world_entity.camera_entity->guid);
	
	auto& renderer_world = world_entity.renderer_world[0];
	auto& camera         = camera_entity.camera[0];
	
	// Clamp render target size to a reasonable minimum. Aspect ratio for view to clip is still computed using unclamped values.
	uint2 render_target_size = uint2((u32)Max(renderer_world.window_size.x, 16.f), (u32)Max(renderer_world.window_size.y, 16.f));
	
	BuildResourceTable(record_context, entity_system, &renderer_world, render_target_size);
	
	
	SceneConstants scene;
	scene.render_target_size     = float2(render_target_size);
	scene.inv_render_target_size = float2(1.f) / scene.render_target_size;
	if (camera.transform_type == CameraTransformType::Perspective) {
		scene.view_to_clip_coef = Math::PerspectiveViewToClip(camera.vertical_fov_degrees * Math::degrees_to_radians, renderer_world.window_size, camera.near_depth);
		scene.clip_to_view_coef = Math::ViewToClipInverse(scene.view_to_clip_coef);
	} else {
		scene.view_to_clip_coef = Math::OrthographicViewToClip(renderer_world.window_size * camera.vertical_fov_degrees * (1.f / renderer_world.window_size.x), 1024.f);
		scene.clip_to_view_coef = Math::ViewToClipInverse(scene.view_to_clip_coef);
	}
	
	auto world_space_camera_position = camera_entity.position->position;
	auto view_to_world_rotation = Math::QuatToRotationMatrix(camera_entity.rotation->rotation);
	scene.view_to_world.r0 = float4(view_to_world_rotation.r0, world_space_camera_position.x);
	scene.view_to_world.r1 = float4(view_to_world_rotation.r1, world_space_camera_position.y);
	scene.view_to_world.r2 = float4(view_to_world_rotation.r2, world_space_camera_position.z);
	
	auto world_to_view_rotation = Math::Transpose(view_to_world_rotation);
	auto view_space_camera_position = world_to_view_rotation * world_space_camera_position;
	scene.world_to_view.r0 = float4(world_to_view_rotation.r0, -view_space_camera_position.x);
	scene.world_to_view.r1 = float4(world_to_view_rotation.r1, -view_space_camera_position.y);
	scene.world_to_view.r2 = float4(world_to_view_rotation.r2, -view_space_camera_position.z);
	
	scene.world_to_pixel_scale = scene.view_to_clip_coef.x * scene.render_target_size.x * 0.5f / Max(renderer_world.meshlet_target_error_pixels, 1.f);
	scene.world_space_camera_position = world_space_camera_position;
	
	AtmosphereParameters atmosphere_parameters;
	atmosphere_parameters.world_space_sun_direction.x = cosf(renderer_world.sun_elevation_degrees * Math::degrees_to_radians);
	atmosphere_parameters.world_space_sun_direction.z = sinf(renderer_world.sun_elevation_degrees * Math::degrees_to_radians);
	
	auto [scene_constants_gpu_address, scene_constants_cpu_address] = AllocateTransientUploadBuffer<SceneConstants>(record_context);
	auto [atmosphere_parameters_gpu_address, atmosphere_parameters_cpu_address] = AllocateTransientUploadBuffer<AtmosphereParameters>(record_context);
	*scene_constants_cpu_address = scene;
	*atmosphere_parameters_cpu_address = atmosphere_parameters;
	
	EntitySystemUpdateRenderPass{ entity_system, renderer_world.gpu_uploads }.RecordPass(record_context);
	TransmittanceLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	MultipleScatteringLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	SkyPanoramaLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	AtmosphereCompositeRenderPass{ atmosphere_parameters_gpu_address, scene_constants_gpu_address }.RecordPass(record_context);
	
	{
		auto [vb_gpu_address, vb_cpu_address] = AllocateTransientUploadBuffer<BasicVertex,  sizeof(BasicVertex)>(record_context,  (u32)renderer_world.vertices.count);
		auto [mb_gpu_address, mb_cpu_address] = AllocateTransientUploadBuffer<BasicMeshlet, sizeof(BasicMeshlet)>(record_context, (u32)renderer_world.meshlets.count);
		auto [ib_gpu_address, ib_cpu_address] = AllocateTransientUploadBuffer<u8, sizeof(uint4)>(record_context, (u32)renderer_world.indices.count);
		memcpy(vb_cpu_address, renderer_world.vertices.data, renderer_world.vertices.count * sizeof(BasicVertex));
		memcpy(mb_cpu_address, renderer_world.meshlets.data, renderer_world.meshlets.count * sizeof(BasicMeshlet));
		memcpy(ib_cpu_address, renderer_world.indices.data,  renderer_world.indices.count  * sizeof(u8));
		
		auto* mesh_entities = QueryEntityTypeArray<MeshEntityType>(*entity_system);
		
		MeshletClearBuffersRenderPass{}.RecordPass(record_context);
		MeshletCullingRenderPass{ scene_constants_gpu_address, mb_gpu_address, (u32)renderer_world.meshlets.count, mesh_entities->count }.RecordPass(record_context);
		BasicMeshRenderPass{ scene_constants_gpu_address, vb_gpu_address, mb_gpu_address, ib_gpu_address, (u32)renderer_world.vertices.count, (u32)renderer_world.meshlets.count, (u32)renderer_world.indices.count }.RecordPass(record_context);
	}
	
	ImGuiRenderPass{}.RecordPass(record_context);
}
