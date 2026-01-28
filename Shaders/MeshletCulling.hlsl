#include "Basic.hlsl"

#if defined(CLEAR_BUFFERS)
[ThreadGroupSize(256, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	if (thread_id == 0) {
		indirect_arguments[0] = uint4(0, 1, 1, 0);
		culling_hzb_build_state[0] = 0;
	}
	
	meshlet_streaming_feedback[thread_id] = thread_id == 0 ? 2 : 0;
}
#endif // defined(CLEAR_BUFFERS)


#if defined(ALLOCATE_STREAMING_FEEDBACK)
compile_const u32 mesh_asset_header_size = 2;

[ThreadGroupSize(256, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint mesh_asset_index = thread_id;
	
	if (BitArrayTestBit(mesh_asset_alive_mask, mesh_asset_index) == false) return;
	
	u32 meshlet_page_count = mesh_asset_data[mesh_asset_index].meshlet_page_count;
	uint feedback_buffer_size = DivideAndRoundUp(meshlet_page_count, 32u);
	
	uint feedback_buffer_offset = 0;
	InterlockedAdd(meshlet_streaming_feedback[0], feedback_buffer_size + mesh_asset_header_size, feedback_buffer_offset);
	InterlockedAdd(meshlet_streaming_feedback[1], meshlet_page_count);
	
	mesh_asset_data[mesh_asset_index].feedback_buffer_offset = feedback_buffer_offset + mesh_asset_header_size;
	meshlet_streaming_feedback[feedback_buffer_offset + 0] = mesh_asset_index;
	meshlet_streaming_feedback[feedback_buffer_offset + 1] = feedback_buffer_size;
}
#endif // defined(ALLOCATE_STREAMING_FEEDBACK)


#if defined(MESHLET_CULLING)
float2 EvaluateMeshletErrorMetric(MeshletErrorMetric error_metric, GpuTransform model_to_world) {
	float2 result;
	result.x = model_to_world.scale * error_metric.error * scene.world_to_pixel_scale;
	
	if (IsPerspectiveMatrix(scene.culling_view_to_clip_coef)) {
		float3 center_world_space = QuatMul(model_to_world.rotation, error_metric.center * model_to_world.scale) + model_to_world.position;
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
	
	// Scale by 0.5 because we're sampling a 2x2 rect.
	float2 rect_size_pixels = scene.culling_hzb_size * (uv_max - uv_min) * 0.5;
	float mip_level = ceil(log2(max(rect_size_pixels.x, rect_size_pixels.y)));
	
	float depth = culling_hzb.SampleLevel(sampler_min_clamp, (uv_max + uv_min) * 0.5, mip_level);
	return clip_space_box.closest_depth >= depth;
}

enum struct VisibilityStatus : uint {
	Visible         = 0,
	FrustumCulled   = 1,
	OcclusionCulled = 2,
};

VisibilityStatus IsModelSpaceBoxVisible(GpuTransform model_to_world, GpuTransform prev_model_to_world, float3 aabb_center, float3 aabb_radius) {
#if defined(MAIN_PASS)
	// Frustum culling using current transform:
	ClipSpaceBoxInfo clip_space_box = TransfromBoxModelToClipSpace(model_to_world, scene.culling_world_to_view, scene.culling_view_to_clip_coef, aabb_center, aabb_radius);
	if (FrustumCullClipSpaceBox(clip_space_box) == false) return VisibilityStatus::FrustumCulled;
	
	// Occlusion culling against previous HZB using previous transform:
	ClipSpaceBoxInfo prev_clip_space_box = TransfromBoxModelToClipSpace(prev_model_to_world, scene.culling_prev_world_to_view, scene.culling_prev_view_to_clip_coef, aabb_center, aabb_radius);
	if (OcclusionCullClipSpaceBox(prev_clip_space_box) == false) return VisibilityStatus::OcclusionCulled;
#endif // defined(MAIN_PASS)
	
#if defined(DISOCCLUSION_PASS)
	// Frustum culling is not needed since we already passed it.
	// Occlusion culling against current HZB using current transform:
	ClipSpaceBoxInfo clip_space_box = TransfromBoxModelToClipSpace(model_to_world, scene.culling_world_to_view, scene.culling_view_to_clip_coef, aabb_center, aabb_radius);
	if (OcclusionCullClipSpaceBox(clip_space_box) == false) return VisibilityStatus::OcclusionCulled;
#endif // defined(DISOCCLUSION_PASS)
	
	return VisibilityStatus::Visible;
}

// TODO: Reorganize the shader to allow culling lists of mesh entities, meshlet groups, and meshlets in the disocclusion pass.
void AppendOccludedMeshEntity(uint mesh_entity_index) {}
void AppendOccludedMeshletGroup(uint mesh_entity_index, uint meshlet_group_index) {}
void AppendOccludedMeshlet(uint mesh_entity_index, uint meshlet_culling_data_offset) {}

compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint2 thread_id : SV_DispatchThreadID) {
	uint mesh_entity_index = thread_id.y;
	
	if (BitArrayTestBit(mesh_alive_mask, mesh_entity_index) == false) return;
	
	uint mesh_asset_index = mesh_entity_data[mesh_entity_index].mesh_asset_index;
	if (mesh_asset_index == u32_max) return;
	
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_asset_index];
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	
	uint mesh_entity_visibility_status = IsModelSpaceBoxVisible(model_to_world, prev_model_to_world, mesh_asset.aabb_center, mesh_asset.aabb_radius);
#if defined(MAIN_PASS)
	if (mesh_entity_visibility_status == VisibilityStatus::OcclusionCulled) AppendOccludedMeshEntity(mesh_entity_index);
#endif // defined(MAIN_PASS)
	if (mesh_entity_visibility_status != VisibilityStatus::Visible) return;
	
	compile_const uint page_residency_mask_size = MeshletPageHeader::max_page_count / 32u;
	uint page_table_offset = mesh_asset.meshlet_group_buffer_offset + mesh_asset.meshlet_group_count * sizeof(MeshletGroup) + page_residency_mask_size * sizeof(uint);
	
	for (uint meshlet_group_index = thread_id.x; meshlet_group_index < mesh_asset.meshlet_group_count; meshlet_group_index += thread_group_size) {
		MeshletGroup group = mesh_asset_buffer.Load<MeshletGroup>(mesh_asset.meshlet_group_buffer_offset + meshlet_group_index * sizeof(MeshletGroup));
		float2 coarser_level_error_metric = EvaluateMeshletErrorMetric(group.error_metric, model_to_world);
		
		bool is_visible = LodCullCoarserLevelError(coarser_level_error_metric);
		if (is_visible == false) continue;
		
		uint group_visibility_status = IsModelSpaceBoxVisible(model_to_world, prev_model_to_world, group.aabb_center, group.aabb_radius); 
#if defined(MAIN_PASS)
		if (group_visibility_status == VisibilityStatus::OcclusionCulled) AppendOccludedMeshletGroup(mesh_entity_index, meshlet_group_index);
#endif // defined(MAIN_PASS)
		if (group_visibility_status != VisibilityStatus::Visible) continue;
		
		// Write streaming feedback for both resident and non resident pages.
		for (uint page_index = group.page_index; page_index < (group.page_index + group.page_count); page_index += 1) {
			InterlockedOr(meshlet_streaming_feedback[mesh_asset.feedback_buffer_offset + (page_index / 32u)], 1u << (page_index % 32u));
		}
		
		if (group.is_resident == 0) continue;
		
		uint meshlet_offset = group.meshlet_offset; // Offset from the beginning of the first page, in meshlets.
		uint meshlet_count  = group.meshlet_count;  // Total meshlet count across all pages.
		for (uint page_index = group.page_index; page_index < (group.page_index + group.page_count); page_index += 1) {
			uint page_offset = mesh_asset_buffer.Load(page_table_offset + page_index * sizeof(uint));
			
			MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
			page_offset += sizeof(MeshletPageHeader);
			
			uint page_meshlet_count = min(page_header.meshlet_count - meshlet_offset, meshlet_count);
			meshlet_count -= page_meshlet_count;
			
			page_offset += meshlet_offset * sizeof(MeshletCullingData);
			meshlet_offset = 0;
			
			for (uint meshlet_index = 0; meshlet_index < page_meshlet_count; meshlet_index += 1) {
				uint meshlet_culling_data_offset = page_offset + meshlet_index * sizeof(MeshletCullingData);
				MeshletCullingData meshlet = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
				
				bool has_higher_level_of_detail = false;
				if (meshlet.current_level_meshlet_group_index != u32_max) {
					uint higher_detail_group_offset = mesh_asset.meshlet_group_buffer_offset + meshlet.current_level_meshlet_group_index * sizeof(MeshletGroup);
					has_higher_level_of_detail = mesh_asset_buffer.Load<MeshletGroup>(higher_detail_group_offset).is_resident != 0;
				}
				
				float2 current_level_error_metric = EvaluateMeshletErrorMetric(meshlet.current_level_error_metric, model_to_world);
				
				bool is_visible = LodCullCurrentLevelError(current_level_error_metric);
				if (is_visible == false && has_higher_level_of_detail) continue;
				
				uint meshlet_visibility_status = IsModelSpaceBoxVisible(model_to_world, prev_model_to_world, meshlet.aabb_center, meshlet.aabb_radius);
#if defined(MAIN_PASS)
				if (meshlet_visibility_status == VisibilityStatus::OcclusionCulled) AppendOccludedMeshlet(mesh_entity_index, meshlet_culling_data_offset);
#endif // defined(MAIN_PASS)
				if (meshlet_visibility_status != VisibilityStatus::Visible) continue;
				
				uint visible_meshlet_index = 0;
				InterlockedAdd(indirect_arguments[0].x, 1u, visible_meshlet_index);
				
				if (visible_meshlet_index < SceneConstants::visible_meshlet_buffer_count) {
					visible_meshlets[visible_meshlet_index] = uint2(meshlet_culling_data_offset + meshlet.meshlet_header_offset, mesh_entity_index);
				} else {
					InterlockedAdd(indirect_arguments[0].x, -1);
				}
			}
		}
	}
}
#endif // defined(MESHLET_CULLING)
