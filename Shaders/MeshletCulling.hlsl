#include "Basic.hlsl"

#if defined(CLEAR_BUFFERS)
[ThreadGroupSize(1, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	indirect_arguments[0] = uint4(0, 1, 1, 0);
}
#endif // defined(CLEAR_BUFFERS)

#if defined(MESHLET_CULLING)
float2 EvaluateMeshletErrorMetric(MeshletErrorMetric error_metric, GpuTransform model_to_world) {
	float2 result;
	result.x = model_to_world.scale * error_metric.error * scene.world_to_pixel_scale;
	
	if (IsPerspectiveMatrix(scene.view_to_clip_coef)) {
		float3 center_world_space = QuatMul(model_to_world.rotation, error_metric.center * model_to_world.scale) + model_to_world.position;
		float  radius_world_space = error_metric.radius * model_to_world.scale;
		
		result.y = max(length(center_world_space - scene.world_space_camera_position) - radius_world_space, 0.f);
	} else {
		result.y = 1.0;
	}
	
	return result;
}

bool LodCullCurrentLevelError(float2 error_metric) { return (error_metric.x <= error_metric.y); }
bool LodCullCoarserLevelError(float2 error_metric) { return (error_metric.x <= error_metric.y) == false; }

compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint2 thread_id : SV_DispatchThreadID) {
	uint mesh_entity_index = thread_id.y;
	
	if (BitArrayTestBit(mesh_alive_mask, mesh_entity_index) == false) return;
	
	uint mesh_asset_index = mesh_entity_data[mesh_entity_index].mesh_asset_index;
	if (mesh_asset_index == u32_max) return;
	
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_asset_index];
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	
	for (uint meshlet_index = thread_id.x; meshlet_index < mesh_asset.meshlet_count; meshlet_index += thread_group_size) {
		BasicMeshlet meshlet = mesh_asset_buffer.Load<BasicMeshlet>(mesh_asset.meshlet_buffer_offset + meshlet_index * sizeof(BasicMeshlet));
		
		float2 current_level_error_metric = EvaluateMeshletErrorMetric(meshlet.current_level_error_metric, model_to_world);
		float2 coarser_level_error_metric = EvaluateMeshletErrorMetric(meshlet.coarser_level_error_metric, model_to_world);
		
		bool is_visible = LodCullCurrentLevelError(current_level_error_metric) && LodCullCoarserLevelError(coarser_level_error_metric);
		if (is_visible == false) continue;
		
		uint visible_meshlet_index = 0;
		InterlockedAdd(indirect_arguments[0].x, 1u, visible_meshlet_index);
		
		if (visible_meshlet_index < SceneConstants::visible_meshlet_buffer_count) {
			visible_meshlets[visible_meshlet_index] = uint2(meshlet_index, mesh_entity_index);
		} else {
			InterlockedAdd(indirect_arguments[0].x, -1);
		}
	}
}
#endif // defined(MESHLET_CULLING)
