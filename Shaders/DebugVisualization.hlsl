#include "Basic.hlsl"

#if defined(DEBUG_VISUALIZATION)
#include "Generated/MeshData.hlsl"

compile_const u32 thread_group_size = 16;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	float3 result = 0.0;
	float depth = depth_stencil[thread_id];
	
	switch (constants.mode) {
	case DebugVisualizationMode::Albedo: {
		float4 albedo_metalness = gb_albedo_metalness[thread_id];
		result = albedo_metalness.xyz;
		break;
	} case DebugVisualizationMode::Normal: {
		float4 normal_roughness = gb_normal_roughness[thread_id];
		float3 world_space_normal = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
		result = world_space_normal * 0.5 + 0.5;
		break;
	} case DebugVisualizationMode::Roughness: {
		float4 normal_roughness = gb_normal_roughness[thread_id];
		result = PlasmaHeatMap(normal_roughness.z);
		break;
	} case DebugVisualizationMode::Metalness: {
		float4 albedo_metalness = gb_albedo_metalness[thread_id];
		result = PlasmaHeatMap(albedo_metalness.w);
		break;
	} case DebugVisualizationMode::Depth: {
		float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
		result = PlasmaHeatMap(sin(log2(view_space_position.z) * TAU) * 0.5 + 0.5);
		break;
	} case DebugVisualizationMode::TriangleID:
	case DebugVisualizationMode::MeshletID:
	case DebugVisualizationMode::MeshEntityID:
	case DebugVisualizationMode::GeometryID:
	case DebugVisualizationMode::MaterialID:
	case DebugVisualizationMode::LevelOfDetail: {
		uint scene_primitive_id = visibility_buffer[thread_id];
		if (scene_primitive_id == 0) break;
		
		u32 visible_meshlet_index = (scene_primitive_id >> 7) - 1;
		uint2 meshlet_instance    = visible_meshlets[visible_meshlet_index];
		u32 meshlet_header_offset = meshlet_instance.x;
		u32 mesh_entity_index     = meshlet_instance.y;
		u32 triangle_index        = (scene_primitive_id & 0x7F);
		
		MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
		GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
		
		switch (constants.mode) {
		case DebugVisualizationMode::TriangleID: {
			result = RandomColor(triangle_index);
			break;
		} case DebugVisualizationMode::MeshletID: {
			result = RandomColor(meshlet_header_offset);
			break;
		} case DebugVisualizationMode::MeshEntityID: {
			result = RandomColor(mesh_entity_index);
			break;
		} case DebugVisualizationMode::GeometryID: {
			result = PlasmaHeatMap(meshlet.geometry_index * rcp(GpuMeshEntityData::max_materials - 1));
			break;
		} case DebugVisualizationMode::MaterialID: {
			result = RandomColor(mesh_entity.material_table[meshlet.geometry_index]);
			break;
		} case DebugVisualizationMode::LevelOfDetail: {
			result = PlasmaHeatMap(Pow2(1.0 - meshlet.level_of_detail_index * rcp(15.0)));
			break;
		} default: {
			break;
		}
		}
		
		break;
	} default: {
		break;
	}
	}
	
	scene_radiance[thread_id] = float4(depth == 0.0 ? 0.0 : result, 1.0);
}
#endif // defined(DEBUG_VISUALIZATION)


#if defined(DEBUG_READBACK)
#include "Generated/MeshData.hlsl"
#include "Generated/DebugVisualizationData.hlsl"

[ThreadGroupSize(1, 1, 1)]
void MainCS() {
	DebugCursorReadback result;
	result.world_space_position = 0.0;
	result.world_space_normal   = 0.0;
	result.mesh_entity_index    = u32_max;
	result.mesh_entity_geometry_index = u32_max;
	
	float depth = depth_stencil[scene.mouse_cursor_position];
	if (depth != 0.0) {
		float2 thread_uv = (scene.mouse_cursor_position + 0.5) * scene.inv_render_target_size;
		float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc);
		result.world_space_position = mul(scene.view_to_world, float4(view_space_position, 1.0));
		
		float4 normal_roughness = gb_normal_roughness[scene.mouse_cursor_position];
		result.world_space_normal = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	}
	
	uint scene_primitive_id = visibility_buffer[scene.mouse_cursor_position];
	if (scene_primitive_id != 0) {
		u32 visible_meshlet_index = (scene_primitive_id >> 7) - 1;
		uint2 meshlet_instance    = visible_meshlets[visible_meshlet_index];
		u32 meshlet_header_offset = meshlet_instance.x;
		u32 mesh_entity_index     = meshlet_instance.y;
		u32 triangle_index        = (scene_primitive_id & 0x7F);
		
		MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
		result.mesh_entity_index = mesh_entity_index;
		result.mesh_entity_geometry_index = meshlet.geometry_index;
	}
	
	readback_buffer.Store(0, result);
}
#endif // defined(DEBUG_READBACK)
