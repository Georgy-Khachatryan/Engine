#include "Basic/Basic.h"
#include "EntitySystem/EntitySystem.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "RenderPasses.h"

static void BuildResourceTable(RecordContext* record_context, WorldEntitySystem* world_system, AssetEntitySystem* asset_system, RendererWorld* renderer_world, uint2 render_target_size) {
	using ID    = VirtualResourceID;
	using Flags = CreateResourceFlags;
	auto& table = *record_context->resource_table;
	
	table.Set(ID::TransmittanceLut,      TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::transmittance_lut_size));
	table.Set(ID::MultipleScatteringLut, TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::multiple_scattering_lut_size));
	table.Set(ID::SkyPanoramaLut,        TextureSize(TextureFormat::R16G16B16A16_FLOAT, AtmosphereParameters::sky_panorama_lut_size));
	
	auto* mesh_assets   = QueryEntityTypeArray<MeshAssetType>(*asset_system);
	auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
	
	table.Set(ID::VisibleMeshlets,             MeshletConstants::visible_meshlet_buffer_size         * sizeof(uint2));
	table.Set(ID::MeshEntityCullingCommands,   MeshletConstants::mesh_entity_culling_command_count   * sizeof(u32));
	table.Set(ID::MeshletGroupCullingCommands, MeshletConstants::meshlet_group_culling_command_count * sizeof(uint2));
	table.Set(ID::MeshletCullingCommands,      MeshletConstants::meshlet_culling_command_count       * sizeof(uint2));
	table.Set(ID::MeshletIndirectArguments,    (u32)MeshletCullingIndirectArgumentsLayout::Count     * sizeof(uint4));
	table.Set(ID::MeshletStreamingFeedback,    gpu_memory_page_size);
	table.Set(ID::MeshStreamingFeedback,       mesh_assets->capacity * sizeof(u32) + sizeof(u32));
	table.Set(ID::TextureStreamingFeedback,    persistent_srv_descriptor_count * sizeof(u32));
	table.Set(ID::InstanceMeshletCounts,       mesh_entities->capacity * sizeof(u32));
	table.Set(ID::TlasMeshInstances,           mesh_entities->capacity * 64u);
	table.Set(ID::MeshletRtasIndirectArguments, (u32)MeshletRtasIndirectArgumentsLayout::Count * sizeof(u32));
	
	auto tlas_requirements = GetTlasMemoryRequirements(record_context->context, { mesh_entities->capacity });
	table.Set(ID::SceneTLAS, AlignUp(tlas_requirements.rtas_max_size_bytes, gpu_memory_page_size), Flags::UAV | Flags::RTAS);
	
	table.Set(ID::SceneRadiance,       TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size), Flags::UAV | Flags::RTV);
	table.Set(ID::DepthStencil,        TextureSize(TextureFormat::D32_FLOAT,          render_target_size), Flags::DSV);
	table.Set(ID::MotionVectors,       TextureSize(TextureFormat::R16G16_FLOAT,       render_target_size), Flags::UAV | Flags::RTV);
	table.Set(ID::SceneRadianceResult, TextureSize(TextureFormat::R16G16B16A16_FLOAT, render_target_size), Flags::UAV);
	
	table.Set(ID::ReferencePathTracerRadiance, TextureSize(TextureFormat::R32G32B32A32_FLOAT, render_target_size), Flags::UAV);
	table.Set(ID::GgxSingleScatteringEnergyLUT, TextureSize(TextureFormat::R16G16_UNORM, 32, 32), Flags::UAV);
	
	table.Set(ID::CullingHZB,           BuildHzbRenderPass::ComputeCullingHzbSize(render_target_size));
	table.Set(ID::CullingHzbBuildState, BuildHzbRenderPass::culling_hzb_build_state_size);
	
	table.Set(ID::DebugGeometryDepthStencil, TextureSize(TextureFormat::D32_FLOAT, render_target_size), Flags::DSV);
}

using RecordPassCallback = void(*)(void*, RecordContext*);

struct RenderPassArrayEntry {
	RecordPassCallback record_pass = nullptr;
	void* render_pass = nullptr;
	String debug_name = "Unknown"_sl;
};

struct RenderPassArray {
	StackAllocator* alloc = nullptr;
	Array<RenderPassArrayEntry> render_passes;
	
	template<typename RenderPassT>
	RenderPassT& Add(String debug_name_substitution = ""_sl) {
		auto* render_pass = NewFromAlloc(alloc, RenderPassT);
		
		RenderPassArrayEntry entry;
		entry.render_pass = render_pass;
		entry.record_pass = [](void* render_pass, RecordContext* record_context) { return ((RenderPassT*)render_pass)->RecordPass(record_context); };
		entry.debug_name  = debug_name_substitution.count != 0 ? debug_name_substitution : RenderPassT::debug_name;
		ArrayAppend(render_passes, alloc, entry);
		
		return *render_pass;
	}
};

static void ReplayRenderPasses(RenderPassArray& array, RecordContext* record_context) {
	for (auto& entry : array.render_passes) {
		CmdProfilerBeginScope(record_context, entry.debug_name);
		entry.record_pass(entry.render_pass, record_context);
		CmdProfilerEndScope(record_context);
	}
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
	
	scene.prev_render_target_size     = scene.render_target_size;
	scene.inv_prev_render_target_size = scene.inv_render_target_size;
	
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
		scene.meshlet_world_to_pixel_scale = scene.view_to_clip_coef.x * scene.render_target_size.x * 0.5f / Math::Max(renderer_world.meshlet_target_error_pixels, 1.f);
		scene.world_space_camera_position  = world_space_camera_position;
		
		auto culling_hzb_size = GetTextureSize(record_context, VirtualResourceID::CullingHZB);
		scene.culling_hzb_size     = float2(culling_hzb_size);
		scene.inv_culling_hzb_size = float2(1.f) / scene.culling_hzb_size;
		
		renderer_world.debug_freeze_culling_camera.view_to_world_rotation = camera_entity.rotation->rotation;
	}
	
	scene.texture_world_to_pixel_scale = scene.view_to_clip_coef.x * scene.render_target_size.x * 0.5f;
	
	u32 jitter_frame_index = renderer_world.jitter_frame_index;
	renderer_world.jitter_frame_index = (jitter_frame_index + 1) % 8;
	
	if (renderer_world.enable_anti_aliasing) {
		scene.jitter_offset_pixels = float2(Math::HaltonSequence(jitter_frame_index, 2), Math::HaltonSequence(jitter_frame_index, 3)) - 0.5f;
		scene.jitter_offset_ndc    = scene.jitter_offset_pixels * float2(2.f, -2.f) / scene.render_target_size;
	} else {
		scene.jitter_offset_pixels = 0.f;
		scene.jitter_offset_ndc    = 0.f;
	}
	
	scene.frame_index = (u32)record_context->frame_index;
	scene.reference_path_tracer_percent = renderer_world.reference_path_tracer_percent;
	
	bool should_reset_path_tracer = renderer_world.reset_reference_path_tracer || memcmp(&scene.view_to_world, &scene.prev_view_to_world, sizeof(float3x4)) != 0 || memcmp(&scene.render_target_size, &scene.prev_render_target_size, sizeof(float2)) != 0;
	if (should_reset_path_tracer) {
		renderer_world.reset_reference_path_tracer = false;
		scene.path_tracer_accumulated_frame_count = 0;
	}
	
	if (renderer_world.reference_path_tracer_percent != 0.f) {
		scene.path_tracer_accumulated_frame_count += 1;
	}
	
	auto gpu_scene_constants = AllocateGpuComponentUploadBuffer(record_context, 1, world_entity.gpu_scene_constants);
	AppendGpuTransferCommand(gpu_scene_constants, 0, scene);
	ArrayAppend(renderer_world.gpu_uploads, record_context->alloc, gpu_scene_constants);
	
	AtmosphereParameters atmosphere_parameters;
	atmosphere_parameters.world_space_sun_direction.x = cosf(renderer_world.sun_elevation_degrees * Math::degrees_to_radians);
	atmosphere_parameters.world_space_sun_direction.z = sinf(renderer_world.sun_elevation_degrees * Math::degrees_to_radians);
	
	auto [atmosphere_parameters_gpu_address, atmosphere_parameters_cpu_address] = AllocateTransientUploadBuffer<AtmosphereParameters>(record_context);
	*atmosphere_parameters_cpu_address = atmosphere_parameters;
	
	
	RenderPassArray render_passes;
	render_passes.alloc = record_context->alloc;
	
	auto& entity_system_update = render_passes.Add<EntitySystemUpdateRenderPass>();
	entity_system_update.world_system = world_system;
	entity_system_update.asset_system = asset_system;
	entity_system_update.upload_buffers = renderer_world.gpu_uploads;
	renderer_world.gpu_uploads = {};
	
	auto& update_meshlet_page_table = render_passes.Add<UpdateMeshletPageTableRenderPass>();
	update_meshlet_page_table.meshlet_streaming_system = renderer_context->meshlet_streaming_system;
	
	render_passes.Add<MeshletClearBuffersRenderPass>().world_system = world_system;
	
	auto& meshlet_rtas_decode_vertex_buffer = render_passes.Add<MeshletRtasDecodeVertexBufferRenderPass>();
	meshlet_rtas_decode_vertex_buffer.meshlet_streaming_system = renderer_context->meshlet_streaming_system;
	
	auto& meshlet_rtas_build = render_passes.Add<MeshletRtasBuildRenderPass>();
	meshlet_rtas_build.meshlet_streaming_system  = renderer_context->meshlet_streaming_system;
	meshlet_rtas_build.mesh_asset_buffer_address = renderer_context->mesh_asset_buffer_address;
	meshlet_rtas_build.scratch_buffer_address    = renderer_context->streaming_scratch_buffer_address;
	
	auto& meshlet_rtas_write_offsets = render_passes.Add<MeshletRtasWriteOffsetsRenderPass>();
	meshlet_rtas_write_offsets.meshlet_streaming_system    = renderer_context->meshlet_streaming_system;
	meshlet_rtas_write_offsets.meshlet_rtas_buffer_address = renderer_context->meshlet_rtas_buffer_address;
	
	auto& meshlet_rtas_update_offsets = render_passes.Add<MeshletRtasUpdateOffsetsRenderPass>();
	meshlet_rtas_update_offsets.meshlet_streaming_system    = renderer_context->meshlet_streaming_system;
	meshlet_rtas_update_offsets.meshlet_rtas_buffer_address = renderer_context->meshlet_rtas_buffer_address;
	
	render_passes.Add<TransmittanceLutRenderPass>().atmosphere = atmosphere_parameters_gpu_address;
	render_passes.Add<MultipleScatteringLutRenderPass>().atmosphere = atmosphere_parameters_gpu_address;
	render_passes.Add<SkyPanoramaLutRenderPass>().atmosphere = atmosphere_parameters_gpu_address;
	
	
	render_passes.Add<MeshletAllocateStreamingFeedbackRenderPass>().asset_system = asset_system;
	
	{
		render_passes.Add<MeshEntityCullingRenderPass>().world_system = world_system;
		render_passes.Add<MeshletGroupCullingRenderPass>();
		render_passes.Add<MeshletCullingRenderPass>();
		render_passes.Add<BasicMeshRenderPass>();
	}
	
	if (renderer_world.debug_freeze_culling_camera.enabled == false) {
		render_passes.Add<BuildHzbRenderPass>();
		
		render_passes.Add<MeshEntityCullingRenderPass>("DisocclusionMeshEntityCulling"_sl).pass = MeshletCullingPass::Disocclusion;
		render_passes.Add<MeshletGroupCullingRenderPass>("DisocclusionMeshletGroupCulling"_sl).pass = MeshletCullingPass::Disocclusion;
		render_passes.Add<MeshletCullingRenderPass>("DisocclusionMeshletCulling"_sl).pass = MeshletCullingPass::Disocclusion;
		render_passes.Add<BasicMeshRenderPass>("DisocclusionBasicMesh"_sl).pass = MeshletCullingPass::Disocclusion;
		
		render_passes.Add<BuildHzbRenderPass>();
	}
	
	{
		auto& raytracing_mesh_entity_culling = render_passes.Add<MeshEntityCullingRenderPass>("RaytracingMeshEntityCulling"_sl);
		raytracing_mesh_entity_culling.pass = MeshletCullingPass::Raytracing;
		raytracing_mesh_entity_culling.world_system = world_system;
		render_passes.Add<MeshletGroupCullingRenderPass>("RaytracingMeshletGroupCulling"_sl).pass = MeshletCullingPass::Raytracing;
		render_passes.Add<MeshletCullingRenderPass>("RaytracingMeshletCulling"_sl).pass = MeshletCullingPass::Raytracing;
	}
	
	render_passes.Add<AtmosphereCompositeRenderPass>().atmosphere = atmosphere_parameters_gpu_address;
	
	auto& copy_meshlet_culling_statistics = render_passes.Add<CopyMeshletCullingStatisticsRenderPass>();
	copy_meshlet_culling_statistics.readback_queue = &renderer_world.meshlet_culling_statistics_readback_queue;
	
	auto& meshlet_blas_build_indirect_arguments = render_passes.Add<MeshletBlasBuildIndirectArgumentsRenderPass>();
	meshlet_blas_build_indirect_arguments.world_system = world_system;
	meshlet_blas_build_indirect_arguments.scratch_buffer_address = renderer_context->streaming_scratch_buffer_address;
	
	auto& meshlet_blas_write_addresses = render_passes.Add<MeshletBlasWriteAddressesRenderPass>();
	meshlet_blas_write_addresses.meshlet_rtas_buffer_address = renderer_context->meshlet_rtas_buffer_address;
	
	auto& build_tlas = render_passes.Add<BuildTlasRenderPass>();
	build_tlas.world_system = world_system;
	
	auto& copy_streaming_feedback = render_passes.Add<CopyStreamingFeedbackRenderPass>();
	copy_streaming_feedback.meshlet_streaming_feedback_queue = &renderer_world.meshlet_streaming_feedback_queue;
	copy_streaming_feedback.mesh_streaming_feedback_queue    = &renderer_world.mesh_streaming_feedback_queue;
	copy_streaming_feedback.texture_streaming_feedback_queue = &renderer_world.texture_streaming_feedback_queue;
	
	
	render_passes.Add<RaytracingDebugRenderPass>();
	
	if (renderer_world.reference_path_tracer_percent != 0.f) {
		render_passes.Add<EnergyCompensationLutRenderPass>();
		render_passes.Add<ReferencePathTracerRenderPass>();
	}
	
	
	auto& debug_geometry = render_passes.Add<DebugGeometryRenderPass>();
	debug_geometry.debug_mesh_instance_arrays = renderer_world.debug_mesh_instance_arrays;
	debug_geometry.debug_geometry_buffer      = &renderer_context->debug_geometry_buffer;
	
	// TODO: Select dynamically.
	// render_passes.Add<XessRenderPass>().jitter_offset_pixels = scene.jitter_offset_pixels;
	render_passes.Add<DlssRenderPass>().jitter_offset_pixels = scene.jitter_offset_pixels;
	
	render_passes.Add<ImGuiRenderPass>();
	
	ReplayRenderPasses(render_passes, record_context);
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
