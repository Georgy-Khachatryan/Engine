#include "Basic/Basic.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "RenderPasses.h"

static void BuildResourceTable(RecordContext* record_context, WorldEntitySystem* world_system, AssetEntitySystem* asset_system, RendererWorld* renderer_world, uint2 render_target_size) {
	using ID = VirtualResourceID;
	auto& table = *record_context->resource_table;
	
	table.virtual_resources.count = (u64)ID::Count;
	table.Set(ID::TransmittanceLut,      TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::transmittance_lut_size));
	table.Set(ID::MultipleScatteringLut, TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::multiple_scattering_lut_size));
	table.Set(ID::SkyPanoramaLut,        TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::sky_panorama_lut_size));
	
	auto* mesh_assets = QueryEntityTypeArray<MeshAssetType>(*asset_system);
	table.Set(ID::VisibleMeshlets, MeshletConstants::visible_meshlet_buffer_size * sizeof(uint2));
	table.Set(ID::MeshEntityCullingCommands, MeshletConstants::mesh_entity_culling_command_count * sizeof(u32));
	table.Set(ID::MeshletGroupCullingCommands, MeshletConstants::meshlet_group_culling_command_count * sizeof(uint2));
	table.Set(ID::MeshletCullingCommands, MeshletConstants::meshlet_culling_command_count * sizeof(uint2));
	table.Set(ID::MeshletIndirectArguments, (u32)MeshletCullingIndirectArgumentsLayout::Count * sizeof(uint4));
	table.Set(ID::MeshletStreamingFeedback, 64u * 1024u);
	table.Set(ID::MeshStreamingFeedback, mesh_assets->capacity * sizeof(u32) + sizeof(u32));
	
	table.Set(ID::SceneRadiance, TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size));
	table.Set(ID::DepthStencil,  TextureSize(TextureFormat::D32_FLOAT, render_target_size));
	table.Set(ID::MotionVectors, TextureSize(TextureFormat::R16G16_FLOAT, render_target_size));
	table.Set(ID::SceneRadianceResult, TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size));
	table.Set(ID::CullingHZB, BuildHzbRenderPass::ComputeCullingHzbSize(render_target_size));
	table.Set(ID::CullingHzbBuildState, BuildHzbRenderPass::culling_hzb_build_state_size);
	
	table.Set(ID::DebugGeometryDepthStencil, TextureSize(TextureFormat::D32_FLOAT, render_target_size));
}

void BuildRenderPassesForFrame(RendererContext* renderer_context, RecordContext* record_context, WorldEntitySystem* world_system, AssetEntitySystem* asset_system, u64 world_entity_guid) {
	ProfilerScope("BuildRenderPassesForFrame");
	
	auto world_entity = QueryEntityByGUID<WorldEntityQuery>(*world_system, world_entity_guid);
	auto camera_entity = QueryEntityByGUID<CameraEntityQuery>(*world_system, world_entity.camera_entity->guid);
	
	auto& renderer_world = world_entity.renderer_world[0];
	auto& camera         = camera_entity.camera[0];
	
	// Clamp render target size to a reasonable minimum. Aspect ratio for view to clip is still computed using unclamped values.
	uint2 render_target_size = uint2((u32)Math::Max(renderer_world.window_size.x, 16.f), (u32)Math::Max(renderer_world.window_size.y, 16.f));
	
	BuildResourceTable(record_context, world_system, asset_system, &renderer_world, render_target_size);
	
	
	auto& scene = renderer_world.scene_constants;
	scene.prev_view_to_clip_coef = scene.view_to_clip_coef;
	scene.prev_clip_to_view_coef = scene.clip_to_view_coef;
	scene.prev_view_to_world     = scene.view_to_world;
	scene.prev_world_to_view     = scene.world_to_view;
	
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
	
	if (renderer_world.debug_freeze_culling_camera.enabled == false) {
		scene.culling_prev_world_to_view     = scene.culling_world_to_view;
		scene.culling_prev_view_to_clip_coef = scene.culling_view_to_clip_coef;
		
		scene.culling_world_to_view     = scene.world_to_view;
		scene.culling_view_to_clip_coef = scene.view_to_clip_coef;
		scene.world_to_pixel_scale      = scene.view_to_clip_coef.x * scene.render_target_size.x * 0.5f / Math::Max(renderer_world.meshlet_target_error_pixels, 1.f);
		scene.world_space_camera_position = world_space_camera_position;
		
		auto culling_hzb_size = GetTextureSize(record_context, VirtualResourceID::CullingHZB);
		scene.culling_hzb_size     = float2(culling_hzb_size);
		scene.inv_culling_hzb_size = float2(1.f) / scene.culling_hzb_size;
		
		renderer_world.debug_freeze_culling_camera.view_to_world_rotation = camera_entity.rotation->rotation;
	}
	
	u32 jitter_frame_index = renderer_world.jitter_frame_index;
	renderer_world.jitter_frame_index = (jitter_frame_index + 1) % 8;
	
	if (renderer_world.enable_anti_aliasing) {
		scene.jitter_offset_pixels = float2(Math::HaltonSequence(jitter_frame_index, 2), Math::HaltonSequence(jitter_frame_index, 3)) - 0.5f;
		scene.jitter_offset_ndc    = scene.jitter_offset_pixels * float2(2.f, -2.f) / scene.render_target_size;
	} else {
		scene.jitter_offset_pixels = 0.f;
		scene.jitter_offset_ndc    = 0.f;
	}
	
	auto gpu_scene_constants = AllocateGpuComponentUploadBuffer(record_context, 1, world_entity.gpu_scene_constants);
	AppendGpuTransferCommand(gpu_scene_constants, 0, scene);
	ArrayAppend(renderer_world.gpu_uploads, record_context->alloc, gpu_scene_constants);
	
	AtmosphereParameters atmosphere_parameters;
	atmosphere_parameters.world_space_sun_direction.x = cosf(renderer_world.sun_elevation_degrees * Math::degrees_to_radians);
	atmosphere_parameters.world_space_sun_direction.z = sinf(renderer_world.sun_elevation_degrees * Math::degrees_to_radians);
	
	auto [atmosphere_parameters_gpu_address, atmosphere_parameters_cpu_address] = AllocateTransientUploadBuffer<AtmosphereParameters>(record_context);
	*atmosphere_parameters_cpu_address = atmosphere_parameters;
	
	EntitySystemUpdateRenderPass{ world_system, asset_system, renderer_world.gpu_uploads }.RecordPass(record_context);
	UpdateMeshletPageTableRenderPass{ renderer_context->meshlet_streaming_system }.RecordPass(record_context);
	TransmittanceLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	MultipleScatteringLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	SkyPanoramaLutRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	AtmosphereCompositeRenderPass{ atmosphere_parameters_gpu_address }.RecordPass(record_context);
	
	MeshletClearBuffersRenderPass{}.RecordPass(record_context);
	MeshletAllocateStreamingFeedbackRenderPass{ asset_system }.RecordPass(record_context);
	
	MeshEntityCullingRenderPass{ world_system }.RecordPass(record_context);
	MeshletGroupCullingRenderPass{ world_system }.RecordPass(record_context);
	MeshletCullingRenderPass{ world_system, &renderer_world.meshlet_streaming_feedback_queue, &renderer_world.mesh_streaming_feedback_queue }.RecordPass(record_context);
	BasicMeshRenderPass{}.RecordPass(record_context);
	
	if (renderer_world.debug_freeze_culling_camera.enabled == false) {
		BuildHzbRenderPass{}.RecordPass(record_context);
		
		MeshEntityCullingRenderPass{ world_system, MeshletCullingPass::Disocclusion }.RecordPass(record_context);
		MeshletGroupCullingRenderPass{ world_system, MeshletCullingPass::Disocclusion }.RecordPass(record_context);
		MeshletCullingRenderPass{ world_system, &renderer_world.meshlet_streaming_feedback_queue, &renderer_world.mesh_streaming_feedback_queue, MeshletCullingPass::Disocclusion }.RecordPass(record_context);
		BasicMeshRenderPass{ MeshletCullingPass::Disocclusion }.RecordPass(record_context);
		
		BuildHzbRenderPass{}.RecordPass(record_context);
	}
	
	DebugGeometryRenderPass{ renderer_world.debug_mesh_instance_arrays, &renderer_context->debug_geometry_buffer }.RecordPass(record_context);
	
	// TODO: Select dynamically.
	// XessRenderPass{ scene.jitter_offset_pixels }.RecordPass(record_context);
	DlssRenderPass{ scene.jitter_offset_pixels }.RecordPass(record_context);
	
	ImGuiRenderPass{}.RecordPass(record_context);
}

GpuComponentUploadBuffer AllocateGpuComponentUploadBuffer(RecordContext* record_context, u32 stride, u32 count, GpuAddress dst_data_gpu_address, GpuAddress dst_prev_data_gpu_address) {
	auto [data_gpu_address,    data_cpu_address]    = AllocateTransientUploadBuffer<u8,  16u>(record_context, count * stride);
	auto [indices_gpu_address, indices_cpu_address] = AllocateTransientUploadBuffer<u32, 16u>(record_context, count);
	
	GpuComponentUploadBuffer result;
	result.count  = 0;
	result.stride = stride;
	result.data_cpu_address     = data_cpu_address;
	result.indices_cpu_address  = indices_cpu_address;
	result.data_gpu_address     = data_gpu_address;
	result.indices_gpu_address  = indices_gpu_address;
	result.dst_data_gpu_address = dst_data_gpu_address;
	result.dst_prev_data_gpu_address = dst_prev_data_gpu_address;
	
	return result;
}
