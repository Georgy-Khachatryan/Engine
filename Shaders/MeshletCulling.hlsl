#include "Basic.hlsl"
#include "Generated/MeshData.hlsl"

#define ENABLE_OCCLUSION_CULLING
#define ENABLE_DISOCCLUSION_PASS

enum struct IndirectArgumentsLayout : u32 {
#if defined(MAIN_PASS)
	DispatchMesh                = MeshletCullingIndirectArgumentsLayout::DispatchMesh,
	MeshletGroupCullingCommands = MeshletCullingIndirectArgumentsLayout::MeshletGroupCullingCommands,
	MeshletCullingCommands      = MeshletCullingIndirectArgumentsLayout::MeshletCullingCommands,
#endif // defined(MAIN_PASS)
	
#if defined(DISOCCLUSION_PASS)
	DispatchMesh                = MeshletCullingIndirectArgumentsLayout::DisocclusionDispatchMesh,
	MeshletGroupCullingCommands = MeshletCullingIndirectArgumentsLayout::DisocclusionMeshletGroupCullingCommands,
	MeshletCullingCommands      = MeshletCullingIndirectArgumentsLayout::DisocclusionMeshletCullingCommands,
#endif // defined(DISOCCLUSION_PASS)
	
#if defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	RetestMeshEntityCullingCommands   = MeshletCullingIndirectArgumentsLayout::RetestMeshEntityCullingCommands,
	RetestMeshletGroupCullingCommands = MeshletCullingIndirectArgumentsLayout::RetestMeshletGroupCullingCommands,
	RetestMeshletCullingCommands      = MeshletCullingIndirectArgumentsLayout::RetestMeshletCullingCommands,
#endif // defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	
#if defined(RAYTRACING_PASS)
	RaytracingBuildBLAS         = MeshletCullingIndirectArgumentsLayout::RaytracingBuildBLAS,
	MeshletGroupCullingCommands = MeshletCullingIndirectArgumentsLayout::RaytracingMeshletGroupCullingCommands,
	MeshletCullingCommands      = MeshletCullingIndirectArgumentsLayout::RaytracingMeshletCullingCommands,
#endif // defined(RAYTRACING_PASS)
};

compile_const uint thread_group_size = MeshletConstants::meshlet_culling_thread_group_size;

compile_const u32 meshlet_feedback_buffer_header_size = 2;
compile_const u32 meshlet_feedback_asset_header_size  = 2;
compile_const u32 mesh_feedback_buffer_header_size    = 1;

#if defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
compile_const MeshletGroupResidencyMask target_residency_mask = MeshletGroupResidencyMask::Page;
#endif // defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)

#if defined(RAYTRACING_PASS)
compile_const MeshletGroupResidencyMask target_residency_mask = MeshletGroupResidencyMask::RTAS;
#endif // defined(RAYTRACING_PASS)


#if defined(CLEAR_BUFFERS)
[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id < MeshletCullingIndirectArgumentsLayout::Count) {
		// We need at least one thread group to copy main pass meshlet count to the disocclusion pass meshlet offset.
		uint min_thread_group_count = thread_id == MeshletCullingIndirectArgumentsLayout::RetestMeshEntityCullingCommands ? 1 : 0;
		indirect_arguments[thread_id] = uint4(min_thread_group_count, 1, 1, 0);
	}
	
	if (thread_id == 0) {
		culling_hzb_build_state[thread_id] = 0;
	}
	
	if (thread_id < constants.meshlet_streaming_feedback_size) {
		meshlet_streaming_feedback[thread_id] = thread_id == 0 ? meshlet_feedback_buffer_header_size : 0;
	}
	
	if (thread_id < constants.mesh_streaming_feedback_size) {
		mesh_streaming_feedback[thread_id] = thread_id == 0 ? constants.mesh_streaming_feedback_size : u32_max;
	}
	
	if (thread_id < constants.texture_streaming_feedback_size) {
		texture_streaming_feedback[thread_id] = u32_max;
	}
	
	if (thread_id < constants.mesh_instance_capacity) {
		instance_meshlet_counts[thread_id] = 0;
	}
}
#endif // defined(CLEAR_BUFFERS)

#if defined(READBACK_STATISTICS)
[ThreadGroupSize(1, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	MeshletCullingStatistics statistics;
	statistics.meshlet_count_main_pass         = indirect_arguments[MeshletCullingIndirectArgumentsLayout::DispatchMesh].x;
	statistics.meshlet_count_disocclusion_pass = indirect_arguments[MeshletCullingIndirectArgumentsLayout::DisocclusionDispatchMesh].x;
	statistics.meshlet_count                   = statistics.meshlet_count_main_pass + statistics.meshlet_count_disocclusion_pass;
	meshlet_culling_statistics.Store(0, statistics);
}
#endif // defined(READBACK_STATISTICS)


#if defined(ALLOCATE_STREAMING_FEEDBACK)
[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint mesh_asset_index = thread_id;
	
	if (BitArrayTestBit(mesh_asset_alive_mask, mesh_asset_index) == false) return;
	
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_asset_index];
	if (mesh_asset.meshlet_group_buffer_offset != u32_max) {
		u32 meshlet_page_count = mesh_asset.meshlet_page_count;
		uint feedback_buffer_size = DivideAndRoundUp(meshlet_page_count, 32u);
		
		uint feedback_buffer_offset = 0;
		InterlockedAdd(meshlet_streaming_feedback[0], feedback_buffer_size + meshlet_feedback_asset_header_size, feedback_buffer_offset);
		InterlockedAdd(meshlet_streaming_feedback[1], meshlet_page_count);
		_Static_assert(meshlet_feedback_buffer_header_size == 2, "Meshlet feedback buffer header code needs to change.");
		
		mesh_asset_data[mesh_asset_index].feedback_buffer_offset = feedback_buffer_offset + meshlet_feedback_asset_header_size;
		meshlet_streaming_feedback[feedback_buffer_offset + 0] = mesh_asset_index;
		meshlet_streaming_feedback[feedback_buffer_offset + 1] = feedback_buffer_size;
		_Static_assert(meshlet_feedback_asset_header_size == 2, "Meshlet feedback asset header code needs to change.");
	} else {
		mesh_asset_data[mesh_asset_index].feedback_buffer_offset = u32_max;
		_Static_assert(mesh_feedback_buffer_header_size == 1, "Mesh feedback buffer header code needs to change.");
	}
}
#endif // defined(ALLOCATE_STREAMING_FEEDBACK)


#if defined(MESH_ENTITY_CULLING) || defined(MESHLET_GROUP_CULLING) || defined(MESHLET_CULLING)
float2 EvaluateMeshletErrorMetric(MeshletErrorMetric error_metric, GpuTransform model_to_world) {
	float2 result;
	result.x = model_to_world.scale * error_metric.error * scene.meshlet_world_to_pixel_scale;
	
	if (IsPerspectiveMatrix(scene.culling_view_to_clip_coef)) {
		float3 center_world_space = TransformModelToWorldSpace(error_metric.center, model_to_world);
		float  radius_world_space = error_metric.radius * model_to_world.scale;
		
		result.y = max(length(center_world_space - scene.world_space_camera_position) - radius_world_space, 0.0);
	} else {
		result.y = 1.0;
	}
	
	return result;
}

bool LodCullCurrentLevelError(float2 error_metric) { return (error_metric.x <= error_metric.y); }
bool LodCullCoarserLevelError(float2 error_metric) { return (error_metric.x <= error_metric.y) == false; }

struct ClipSpaceBoxInfo {
	float2 ndc_min;
	float2 ndc_max;
	float closest_depth;
	bool clips_near_plane;
	uint visible_plane_mask;
};

ClipSpaceBoxInfo TransfromBoxModelToClipSpace(GpuTransform model_to_world, float3x4 world_to_view, float4 view_to_clip_coef, float3 aabb_center, float3 aabb_radius) {
	ClipSpaceBoxInfo result;
	result.visible_plane_mask = 0;
	result.ndc_min = +128.0;
	result.ndc_max = -128.0;
	result.closest_depth = 0.0;
	result.clips_near_plane = false;
	
	float3x4 clip_space_radius; // TODO: Optimize. Precompute rotation matrix, classify corners, etc.
	clip_space_radius[0] = TransformModelToClipSpaceDirection(float3(aabb_radius.x, 0.0, 0.0), model_to_world, world_to_view, view_to_clip_coef);
	clip_space_radius[1] = TransformModelToClipSpaceDirection(float3(0.0, aabb_radius.y, 0.0), model_to_world, world_to_view, view_to_clip_coef);
	clip_space_radius[2] = TransformModelToClipSpaceDirection(float3(0.0, 0.0, aabb_radius.z), model_to_world, world_to_view, view_to_clip_coef);
	float4 clip_space_center = TransformModelToClipSpace(aabb_center, model_to_world, world_to_view, view_to_clip_coef);
	
	for (uint i = 0; i < 8; i += 1) {
		float3 uv_space_corner   = float3(i & 0x1, (i >> 1) & 0x1, (i >> 2) & 0x1) * 2.0 - 1.0;
		float4 clip_space_corner = clip_space_center + mul(uv_space_corner, clip_space_radius);
		
		if (clip_space_corner.x > -clip_space_corner.w) result.visible_plane_mask |= 0x1;
		if (clip_space_corner.x < +clip_space_corner.w) result.visible_plane_mask |= 0x2;
		if (clip_space_corner.y > -clip_space_corner.w) result.visible_plane_mask |= 0x4;
		if (clip_space_corner.y < +clip_space_corner.w) result.visible_plane_mask |= 0x8;
		if (clip_space_corner.z > 0.0)                  result.visible_plane_mask |= 0x10;
		if (clip_space_corner.z < +clip_space_corner.w) result.visible_plane_mask |= 0x20;
		
		bool corner_clips_near_plane = (clip_space_corner.z >= clip_space_corner.w);
		if (corner_clips_near_plane == false) {
			float3 ndc = clip_space_corner.xyz / clip_space_corner.w;
			result.ndc_min = min(ndc.xy, result.ndc_min);
			result.ndc_max = max(ndc.xy, result.ndc_max);
			result.closest_depth = max(ndc.z, result.closest_depth);
		}
		
		result.clips_near_plane |= corner_clips_near_plane;
	}
	
	return result;
}

bool FrustumCullClipSpaceBox(ClipSpaceBoxInfo clip_space_box) {
	return clip_space_box.visible_plane_mask == 0x3F;
}

bool OcclusionCullClipSpaceBox(ClipSpaceBoxInfo clip_space_box) {
	if (clip_space_box.clips_near_plane) return true;
	
	float2 uv_min = saturate(NdcToScreenUv(float2(clip_space_box.ndc_min.x, clip_space_box.ndc_max.y)));
	float2 uv_max = saturate(NdcToScreenUv(float2(clip_space_box.ndc_max.x, clip_space_box.ndc_min.y)));
	
	// Shrink wrap UV bounds around pixel centers.
	uv_min = (round(uv_min * scene.render_target_size) + 0.5) * scene.inv_render_target_size;
	uv_max = (round(uv_max * scene.render_target_size) - 0.5) * scene.inv_render_target_size;
	if (any(uv_min >= uv_max)) return false;
	
	// Scale by 0.25 because we're sampling a 4x4 rect.
	float2 rect_size_pixels = scene.culling_hzb_size * (uv_max - uv_min) * 0.25;
	float mip_level = ceil(log2(max(rect_size_pixels.x, rect_size_pixels.y)));
	
	float2 hzb_mip_size  = scene.culling_hzb_size * exp2(-mip_level);
	float2 hzb_pixel_min = floor(uv_min * hzb_mip_size);
	float2 hzb_pixel_max = ceil( uv_max * hzb_mip_size);
	
	// Sample a higher MIP level if the footprint is larger than 4x4 pixels.
	if (any((hzb_pixel_max - hzb_pixel_min) > 4.0)) {
		mip_level    += 1.0;
		hzb_pixel_min = floor(hzb_pixel_min * 0.5);
		hzb_pixel_max = ceil( hzb_pixel_max * 0.5);
	}
	
	float2 inv_hzb_mip_size = scene.inv_culling_hzb_size * exp2(mip_level);
	float4 sample_rect = float4(hzb_pixel_min + 1.0, hzb_pixel_max - 1.0) * inv_hzb_mip_size.xyxy;
	
	float sample0 = culling_hzb.SampleLevel(sampler_min_clamp, sample_rect.xy, mip_level);
	float sample1 = culling_hzb.SampleLevel(sampler_min_clamp, sample_rect.xw, mip_level);
	float sample2 = culling_hzb.SampleLevel(sampler_min_clamp, sample_rect.zy, mip_level);
	float sample3 = culling_hzb.SampleLevel(sampler_min_clamp, sample_rect.zw, mip_level);
	
	float farthest_depth = min(min(sample0, sample1), min(sample2, sample3));
	return clip_space_box.closest_depth >= farthest_depth;
}

enum struct VisibilityStatus : uint {
	Visible         = 0,
	FrustumCulled   = 1,
	OcclusionCulled = 2,
};

VisibilityStatus IsModelSpaceBoxVisible(GpuTransform model_to_world, GpuTransform prev_model_to_world, float3 aabb_center, float3 aabb_radius) {
#if defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	// Frustum culling using current transform:
	ClipSpaceBoxInfo clip_space_box = TransfromBoxModelToClipSpace(model_to_world, scene.culling_world_to_view, scene.culling_view_to_clip_coef, aabb_center, aabb_radius);
	if (FrustumCullClipSpaceBox(clip_space_box) == false) return VisibilityStatus::FrustumCulled;
#endif // defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	
#if defined(MAIN_PASS) && defined(ENABLE_OCCLUSION_CULLING)
	// Occlusion culling against previous HZB using previous transform:
	ClipSpaceBoxInfo prev_clip_space_box = TransfromBoxModelToClipSpace(prev_model_to_world, scene.culling_prev_world_to_view, scene.culling_prev_view_to_clip_coef, aabb_center, aabb_radius);
	if (OcclusionCullClipSpaceBox(prev_clip_space_box) == false) return VisibilityStatus::OcclusionCulled;
#endif // defined(MAIN_PASS) && defined(ENABLE_OCCLUSION_CULLING)
	
#if defined(DISOCCLUSION_PASS) && defined(ENABLE_OCCLUSION_CULLING)
	// Occlusion culling against current HZB using current transform:
	if (OcclusionCullClipSpaceBox(clip_space_box) == false) return VisibilityStatus::OcclusionCulled;
#endif // defined(DISOCCLUSION_PASS) && defined(ENABLE_OCCLUSION_CULLING)
	
#if defined(RAYTRACING_PASS)
	// TODO: Apply culling applicable to raytracing.
#endif // defined(RAYTRACING_PASS)
	
	return VisibilityStatus::Visible;
}
#endif // defined(MESH_ENTITY_CULLING) || defined(MESHLET_GROUP_CULLING) || defined(MESHLET_CULLING)


#if defined(MESH_ENTITY_CULLING)
void AppendOccludedMeshEntity(uint mesh_entity_index) {
#if defined(MAIN_PASS) && defined(ENABLE_DISOCCLUSION_PASS)
	uint command_index = 0;
	InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::RetestMeshEntityCullingCommands].w, 1u, command_index);
	
	if (command_index < MeshletConstants::mesh_entity_culling_command_bin_size) {
		mesh_entity_culling_commands[command_index] = mesh_entity_index;
		InterlockedMax(indirect_arguments[IndirectArgumentsLayout::RetestMeshEntityCullingCommands].x, DivideAndRoundUp(command_index + 1, thread_group_size));
	}
#endif // defined(MAIN_PASS) && defined(ENABLE_DISOCCLUSION_PASS)
}

void AppendMeshletGroupCullingCommand(uint mesh_entity_index, uint meshlet_group_offset, uint bin_index) {
	uint command_index = 0;
	InterlockedAdd(indirect_arguments[bin_index + IndirectArgumentsLayout::MeshletGroupCullingCommands].w, 1u, command_index);
	
	if (command_index < MeshletConstants::meshlet_group_culling_command_bin_size) {
		uint bin_base_offset = bin_index * MeshletConstants::meshlet_group_culling_command_bin_size;
		meshlet_group_culling_commands[bin_base_offset + command_index] = uint2(meshlet_group_offset, mesh_entity_index);
		InterlockedMax(indirect_arguments[bin_index + IndirectArgumentsLayout::MeshletGroupCullingCommands].x, DivideAndRoundUp((command_index + 1) << bin_index, thread_group_size));
	}
}

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
#if defined(MAIN_PASS) || defined(RAYTRACING_PASS)
	uint mesh_entity_index = thread_id;
	if (BitArrayTestBit(mesh_alive_mask, mesh_entity_index) == false) return;
#elif defined(DISOCCLUSION_PASS)
	// There is always at least one thread group dispatched to perform this copy.
	if (thread_id == 0) {
		uint main_pass_meshlet_count = indirect_arguments[MeshletCullingIndirectArgumentsLayout::DispatchMesh].x;
		indirect_arguments[MeshletCullingIndirectArgumentsLayout::DisocclusionDispatchMesh].w = main_pass_meshlet_count;
	}
	
	if (thread_id >= indirect_arguments[IndirectArgumentsLayout::RetestMeshEntityCullingCommands].w) return;
	uint mesh_entity_index = mesh_entity_culling_commands[thread_id];
#endif // defined(DISOCCLUSION_PASS)
	
	uint mesh_asset_index = mesh_entity_data[mesh_entity_index].mesh_asset_index;
	if (mesh_asset_index == u32_max) return;
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	
#if defined(MAIN_PASS)
	float distance_to_camera = length(model_to_world.position - scene.world_space_camera_position);
	InterlockedMin(mesh_streaming_feedback[mesh_asset_index + mesh_feedback_buffer_header_size], asuint(distance_to_camera));
#endif // defined(MAIN_PASS)
	
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_asset_index];
	if (mesh_asset.meshlet_group_buffer_offset == u32_max) return;
	
	uint mesh_entity_visibility_status = IsModelSpaceBoxVisible(model_to_world, prev_model_to_world, mesh_asset.aabb_center, mesh_asset.aabb_radius);
	if (mesh_entity_visibility_status == VisibilityStatus::OcclusionCulled) {
		AppendOccludedMeshEntity(mesh_entity_index);
	}
	if (mesh_entity_visibility_status != VisibilityStatus::Visible) return;
	
	compile_const uint last_bin_index = MeshletConstants::meshlet_group_culling_command_bin_count - 1;
	
	uint meshlet_group_count  = mesh_asset.meshlet_group_count;
	uint meshlet_group_offset = 0;
	while (meshlet_group_count >= (1u << last_bin_index)) {
		AppendMeshletGroupCullingCommand(mesh_entity_index, meshlet_group_offset, last_bin_index);
		
		meshlet_group_count  -= (1u << last_bin_index);
		meshlet_group_offset += (1u << last_bin_index);
	}
	
	while (meshlet_group_count != 0) {
		uint bin_index = firstbitlow(meshlet_group_count);
		AppendMeshletGroupCullingCommand(mesh_entity_index, meshlet_group_offset, bin_index);
		
		meshlet_group_count  -= (1u << bin_index);
		meshlet_group_offset += (1u << bin_index);
	}
}
#endif // defined(MESH_ENTITY_CULLING)


#if defined(MESHLET_GROUP_CULLING)
void AppendOccludedMeshletGroup(uint mesh_entity_index, uint meshlet_group_index) {
#if defined(MAIN_PASS) && defined(ENABLE_DISOCCLUSION_PASS)
	uint command_index = 0;
	InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::RetestMeshletGroupCullingCommands].w, 1u, command_index);
	
	if (command_index < MeshletConstants::meshlet_group_culling_command_bin_size) {
		uint bin_base_offset = MeshletConstants::meshlet_group_culling_command_bin_count * MeshletConstants::meshlet_group_culling_command_bin_size;
		meshlet_group_culling_commands[bin_base_offset + command_index] = uint2(meshlet_group_index, mesh_entity_index);
		InterlockedMax(indirect_arguments[IndirectArgumentsLayout::RetestMeshletGroupCullingCommands].x, DivideAndRoundUp(command_index + 1, thread_group_size));
	}
#endif // defined(MAIN_PASS) && defined(ENABLE_DISOCCLUSION_PASS)
}

void AppendMeshletCullingCommand(uint mesh_entity_index, uint meshlet_culling_data_offset, uint bin_index) {
	uint visible_group_index = 0;
	InterlockedAdd(indirect_arguments[bin_index + IndirectArgumentsLayout::MeshletCullingCommands].w, 1u, visible_group_index);
	
	if (visible_group_index < MeshletConstants::meshlet_culling_command_bin_size) {
		uint bin_base_offset = bin_index * MeshletConstants::meshlet_culling_command_bin_size;
		meshlet_culling_commands[bin_base_offset + visible_group_index] = uint2(meshlet_culling_data_offset, mesh_entity_index);
		InterlockedMax(indirect_arguments[bin_index + IndirectArgumentsLayout::MeshletCullingCommands].x, DivideAndRoundUp((visible_group_index + 1) << bin_index, thread_group_size));
	}
}

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID, uint thread_index : SV_GroupIndex) {
#if defined(MAIN_PASS) || defined(RAYTRACING_PASS)
	compile_const bool is_disocclusion_bin = false;
#elif defined(DISOCCLUSION_PASS)
	bool is_disocclusion_bin = (constants.bin_index == MeshletConstants::disocclusion_bin_index);
#endif // defined(MAIN_PASS)
	
	uint2 meshlet_group_culling_command;
	if (is_disocclusion_bin == false) {
		if ((thread_id >> constants.bin_index) >= indirect_arguments[constants.bin_index + IndirectArgumentsLayout::MeshletGroupCullingCommands].w) return;
		
		uint bin_base_offset = constants.bin_index * MeshletConstants::meshlet_group_culling_command_bin_size;
		meshlet_group_culling_command = meshlet_group_culling_commands[bin_base_offset + (thread_id >> constants.bin_index)];
		meshlet_group_culling_command.x += thread_id & CreateBitMaskSmall(constants.bin_index);
	} else {
#if defined(DISOCCLUSION_PASS)
		if (thread_id >= indirect_arguments[IndirectArgumentsLayout::RetestMeshletGroupCullingCommands].w) return;
		
		uint bin_base_offset = MeshletConstants::meshlet_group_culling_command_bin_count * MeshletConstants::meshlet_group_culling_command_bin_size;
		meshlet_group_culling_command = meshlet_group_culling_commands[bin_base_offset + thread_id];
#endif // defined(DISOCCLUSION_PASS)
	}
	
	uint meshlet_group_index = meshlet_group_culling_command.x;
	uint mesh_entity_index   = meshlet_group_culling_command.y;
	
	uint mesh_asset_index = mesh_entity_data[mesh_entity_index].mesh_asset_index;
	
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_asset_index];
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	
	uint page_residency_mask_size = DivideAndRoundUp(mesh_asset.meshlet_page_count, 32u);
	uint page_table_offset = mesh_asset.meshlet_group_buffer_offset + mesh_asset.meshlet_group_count * sizeof(MeshletGroup) + page_residency_mask_size * sizeof(uint) * 2;
	
	MeshletGroup group = mesh_asset_buffer.Load<MeshletGroup>(mesh_asset.meshlet_group_buffer_offset + meshlet_group_index * sizeof(MeshletGroup));
	float2 coarser_level_error_metric = EvaluateMeshletErrorMetric(group.error_metric, model_to_world);
	if (LodCullCoarserLevelError(coarser_level_error_metric) == false) return;
	
	uint group_visibility_status = IsModelSpaceBoxVisible(model_to_world, prev_model_to_world, group.aabb_center, group.aabb_radius); 
	if (group_visibility_status == VisibilityStatus::OcclusionCulled) {
		AppendOccludedMeshletGroup(mesh_entity_index, meshlet_group_index);
	}
	if (group_visibility_status != VisibilityStatus::Visible) return;
	
	uint begin_page_index = group.page_index;
	uint end_page_index   = group.page_index + group.page_count;
	
#if defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	// Write streaming feedback for both resident and non resident pages.
	for (uint page_index = begin_page_index; page_index < end_page_index; page_index += 1) {
		InterlockedOr(meshlet_streaming_feedback[mesh_asset.feedback_buffer_offset + (page_index / 32u)], 1u << (page_index % 32u));
	}
#endif // defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	
	if ((group.residency_mask & target_residency_mask) == 0) return;
	
	uint meshlet_offset = group.meshlet_offset; // Offset from the beginning of the first page, in meshlets.
	uint meshlet_count  = group.meshlet_count;  // Total meshlet count across all pages.
	for (uint page_index = begin_page_index; page_index < end_page_index; page_index += 1) {
		uint page_offset = mesh_asset_buffer.Load(page_table_offset + page_index * sizeof(uint));
		MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
		
		uint page_meshlet_count = min(page_header.meshlet_count - meshlet_offset, meshlet_count);
		meshlet_count -= page_meshlet_count;
		
		uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader);
		
		uint page_meshlet_offset = meshlet_offset;
		while (page_meshlet_count != 0) {
			uint bin_index = firstbitlow(page_meshlet_count);
			AppendMeshletCullingCommand(mesh_entity_index, meshlet_culling_data_offset + page_meshlet_offset * sizeof(MeshletCullingData), bin_index);
			
			page_meshlet_count  -= (1u << bin_index);
			page_meshlet_offset += (1u << bin_index);
		}
		
		meshlet_offset = 0;
	}
}
#endif // defined(MESHLET_GROUP_CULLING)


#if defined(MESHLET_CULLING)
void AppendOccludedMeshlet(uint mesh_entity_index, uint meshlet_culling_data_offset) {
#if defined(MAIN_PASS) && defined(ENABLE_DISOCCLUSION_PASS)
	uint command_index = 0;
	InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::RetestMeshletCullingCommands].w, 1u, command_index);
	
	if (command_index < MeshletConstants::meshlet_culling_command_bin_size) {
		uint bin_base_offset = MeshletConstants::meshlet_culling_command_bin_count * MeshletConstants::meshlet_culling_command_bin_size;
		meshlet_culling_commands[bin_base_offset + command_index] = uint2(meshlet_culling_data_offset, mesh_entity_index);
		InterlockedMax(indirect_arguments[IndirectArgumentsLayout::RetestMeshletCullingCommands].x, DivideAndRoundUp(command_index + 1, thread_group_size));
	}
#endif // defined(MAIN_PASS) && defined(ENABLE_DISOCCLUSION_PASS)
}

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID, uint thread_index : SV_GroupIndex) {
#if defined(MAIN_PASS) || defined(RAYTRACING_PASS)
	compile_const bool is_disocclusion_bin = false;
#elif defined(DISOCCLUSION_PASS)
	bool is_disocclusion_bin = (constants.bin_index == MeshletConstants::disocclusion_bin_index);
#endif // defined(MAIN_PASS)
	
	uint2 meshlet_culling_command;
	if (is_disocclusion_bin == false) {
		if ((thread_id >> constants.bin_index) >= indirect_arguments[constants.bin_index + IndirectArgumentsLayout::MeshletCullingCommands].w) return;
		
		uint bin_base_offset = constants.bin_index * MeshletConstants::meshlet_culling_command_bin_size;
		meshlet_culling_command = meshlet_culling_commands[bin_base_offset + (thread_id >> constants.bin_index)];
		meshlet_culling_command.x += (thread_id & CreateBitMaskSmall(constants.bin_index)) * sizeof(MeshletCullingData);
	} else {
#if defined(DISOCCLUSION_PASS)
		if (thread_id >= indirect_arguments[IndirectArgumentsLayout::RetestMeshletCullingCommands].w) return;
		
		uint bin_base_offset = MeshletConstants::meshlet_culling_command_bin_count * MeshletConstants::meshlet_culling_command_bin_size;
		meshlet_culling_command = meshlet_culling_commands[bin_base_offset + thread_id];
#endif // defined(DISOCCLUSION_PASS)
	}
	
	uint meshlet_culling_data_offset = meshlet_culling_command.x;
	uint mesh_entity_index = meshlet_culling_command.y;
	
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_entity.mesh_asset_index];
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	
	MeshletCullingData meshlet = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
	
	bool has_higher_level_of_detail = false;
	if (meshlet.current_level_meshlet_group_index != u32_max) {
		uint higher_detail_group_offset = mesh_asset.meshlet_group_buffer_offset + meshlet.current_level_meshlet_group_index * sizeof(MeshletGroup);
		uint group_residency_mask  = mesh_asset_buffer.Load<u16>(higher_detail_group_offset + MeshletGroup::offset_of_residency_mask);
		has_higher_level_of_detail = (group_residency_mask & target_residency_mask) != 0;
	}
	
	float2 current_level_error_metric = EvaluateMeshletErrorMetric(meshlet.current_level_error_metric, model_to_world);
	
	bool is_visible = LodCullCurrentLevelError(current_level_error_metric);
	if (is_visible == false && has_higher_level_of_detail) return;
	
	uint meshlet_visibility_status = IsModelSpaceBoxVisible(model_to_world, prev_model_to_world, meshlet.aabb_center, meshlet.aabb_radius);
	if (meshlet_visibility_status == VisibilityStatus::OcclusionCulled) {
		AppendOccludedMeshlet(mesh_entity_index, meshlet_culling_data_offset);
	}
	if (meshlet_visibility_status != VisibilityStatus::Visible) return;
	
#if defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	// Write streaming feedback for textures.
	if (mesh_entity.material_asset_index != u32_max) {
		compile_const float texture_size_mip_0 = 4096.0;
		GpuMaterialTextureData material = material_texture_data[mesh_entity.material_asset_index];
		
		float world_to_pixel_scale = scene.view_to_clip_coef.x * scene.render_target_size.x * 0.5;
		float3 center_world_space  = TransformModelToWorldSpace(meshlet.aabb_center, model_to_world);
		
		float numerator    = length(center_world_space - scene.world_space_camera_position);
		float denominator  = model_to_world.scale * scene.texture_world_to_pixel_scale;
		float pixel_to_uv_scale    = meshlet.world_to_uv_scale * (numerator / denominator);
		float pixel_to_texel_scale = pixel_to_uv_scale * texture_size_mip_0;
		
		// pixel_to_texel_scale is approximately the same as max(length(ddx(uv)), length(ddy(uv))) * texture_size_mip_0.
		float target_mip_level = max(log2(pixel_to_texel_scale), 0.0);
		
		InterlockedMin(texture_streaming_feedback[material.albedo], asuint(target_mip_level));
		InterlockedMin(texture_streaming_feedback[material.normal], asuint(target_mip_level));
	}
#endif // defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	
#if defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	uint visible_meshlet_index = 0;
	InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::DispatchMesh].w, 1u, visible_meshlet_index);
	
	if (visible_meshlet_index < MeshletConstants::visible_meshlet_buffer_size) {
		visible_meshlets[visible_meshlet_index] = uint2(meshlet_culling_data_offset + meshlet.meshlet_header_offset, mesh_entity_index);
		InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::DispatchMesh].x, 1u);
	}
#endif // defined(MAIN_PASS) || defined(DISOCCLUSION_PASS)
	
#if defined(RAYTRACING_PASS)
	uint visible_meshlet_index = 0;
	InterlockedAdd(indirect_arguments[IndirectArgumentsLayout::RaytracingBuildBLAS].w, 1u, visible_meshlet_index);
	
	if (visible_meshlet_index < MeshletConstants::visible_meshlet_buffer_size) {
		InterlockedAdd(instance_meshlet_counts[mesh_entity_index], 1u);
		
		visible_meshlets[visible_meshlet_index] = uint2(meshlet_culling_data_offset + meshlet.meshlet_header_offset, mesh_entity_index);
		InterlockedMax(indirect_arguments[IndirectArgumentsLayout::RaytracingBuildBLAS].x, DivideAndRoundUp(visible_meshlet_index + 1, 256u));
	}
#endif // defined(RAYTRACING_PASS)
}
#endif // defined(MESHLET_CULLING)
