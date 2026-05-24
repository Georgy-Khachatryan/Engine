#include "Basic.hlsl"
#include "MeshletVertexDecoding.hlsl"

struct InputPS {
	float4 position : SV_Position;
};

struct InputPrimitivePS {
	uint scene_primitive_id : SCENE_PRIMITIVE_ID;
};

#if defined(MESH_SHADER)
[OutputTopology("triangle")]
[ThreadGroupSize(128, 1, 1)]
void MainMS(
	uint group_id : SV_GroupID,
	uint thread_index : SV_GroupIndex,
	out vertices InputPS result_vertices[128],
	out indices uint3 result_indices[128],
	out primitives InputPrimitivePS result_primitives[128]) {
	
	uint visible_meshlet_index = group_id;
	if (constants.pass == MeshletCullingPass::Disocclusion) {
		visible_meshlet_index += indirect_arguments[MeshletCullingIndirectArgumentsLayout::DispatchMesh].x;
	}
	
	uint2 meshlet_instance     = visible_meshlets[visible_meshlet_index];
	uint meshlet_header_offset = meshlet_instance.x;
	uint mesh_entity_index     = meshlet_instance.y;
	
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_entity.mesh_asset_index];
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	
	SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);
	
	MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
	
	if (thread_index < meshlet.vertex_count) {
		MeshletVertex vertex = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, thread_index);
		float3 vertex_position = DecodeMeshletVertexPosition(vertex.position, mesh_asset, meshlet);
		
		InputPS output;
		output.position = TransformModelToClipSpace(vertex_position, model_to_world, scene.world_to_view, scene.view_to_clip_coef);
		output.position.xy += scene.jitter_offset_ndc * output.position.w;
		
		result_vertices[thread_index] = output;
	}
	
	if (thread_index < meshlet.triangle_count) {
		result_indices[thread_index] = LoadMeshletIndexBuffer(mesh_asset_buffer, offsets.index_buffer_offset, thread_index);
		
		InputPrimitivePS output;
		output.scene_primitive_id = ((visible_meshlet_index + 1) << 7) | thread_index;
		
		result_primitives[thread_index] = output;
	}
}
#endif // defined(MESH_SHADER)

#if defined(PIXEL_SHADER)
struct OutputPS {
	uint scene_primitive_id : SV_Target0;
};

OutputPS MainPS(InputPrimitivePS primitive_input) {
	OutputPS output;
	output.scene_primitive_id = primitive_input.scene_primitive_id;
	
	return output;
}
#endif // defined(PIXEL_SHADER)
