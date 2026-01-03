#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD;
	float3 normal   : NORMAL;
};

struct InputPrimitivePS {
	uint meshlet_index : MESHLET_INDEX;
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
	
	uint2 meshlet_instance = visible_meshlets[group_id]; 
	
	uint mesh_entity_index    = meshlet_instance.y;
	uint meshlet_index        = meshlet_instance.x;
	uint mesh_asset_entity_id = mesh_entity_data[mesh_entity_index].mesh_asset_entity_id;
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_asset_entity_id];
	
	BasicMeshlet meshlet = mesh_asset_buffer.Load<BasicMeshlet>(mesh_asset.meshlet_buffer_offset + meshlet_index * sizeof(BasicMeshlet));
	
	SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);
	
	if (thread_index < meshlet.vertex_count) {
		BasicVertex vertex = mesh_asset_buffer.Load<BasicVertex>(mesh_asset.vertex_buffer_offset + (meshlet.vertex_buffer_offset + thread_index) * sizeof(BasicVertex));
		
		float3 world_space_position = QuatMul(model_to_world.rotation, vertex.position * model_to_world.scale) + model_to_world.position;
		float3 view_space_position  = mul(scene.world_to_view, float4(world_space_position, 1.0));
		
		InputPS output;
		output.position = TransformViewToClipSpace(view_space_position, scene.view_to_clip_coef);
		output.texcoord = vertex.texcoord;
		output.normal   = vertex.normal;
		result_vertices[thread_index] = output;
	}
	
	if (thread_index < meshlet.triangle_count) {
		uint load_offset = mesh_asset.index_buffer_offset + (meshlet.index_buffer_offset + thread_index * 3);
		uint2 packed_indices = mesh_asset_buffer.Load<uint2>(load_offset & ~0x3);
		
		uint indices = uint((u64(packed_indices.x) | (u64(packed_indices.y) << 32)) >> ((load_offset & 0x3) * 8));
		result_indices[thread_index] = uint3(indices >> 0, indices >> 8, indices >> 16) & 0xFF;
		result_primitives[thread_index].meshlet_index = meshlet_index;
	}
}
#endif // defined(MESH_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input, InputPrimitivePS primitive_input, float3 bary : SV_Barycentrics) : SV_Target0 {
	float3 normal_color   = input.normal * 0.5 + 0.5;
	float3 texcoord_color = float3(input.texcoord, 0.0);
	
	float3 meshlet_color = (primitive_input.meshlet_index & 0x1) ? normal_color : texcoord_color;
	float  wireframe     = BarycentricWireframe(bary, ddx(bary), ddy(bary));
	
	return float4(meshlet_color * wireframe, 1.0);
}
#endif // defined(PIXEL_SHADER)
