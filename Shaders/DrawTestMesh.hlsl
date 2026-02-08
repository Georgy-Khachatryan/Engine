#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD;
	float3 normal   : NORMAL;
	
	float3 curr_position : CURR_POSITION;
	float3 prev_position : PREV_POSITION;
};

struct InputPrimitivePS {
	uint meshlet_index  : MESHLET_INDEX;
	uint material_index : MATERIAL_INDEX;
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
	
	uint mesh_entity_index = meshlet_instance.y;
	uint meshlet_header_offset = meshlet_instance.x;
	
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_entity.mesh_asset_index];
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	
	SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);
	
	uint vertex_buffer_offset = meshlet_header_offset + sizeof(MeshletHeader);
	uint index_buffer_offset  = vertex_buffer_offset + meshlet.vertex_count * sizeof(MeshletVertex);
	
	if (thread_index < meshlet.vertex_count) {
		MeshletVertex vertex = mesh_asset_buffer.Load<MeshletVertex>(vertex_buffer_offset + thread_index * sizeof(MeshletVertex));
		float3 vertex_position = float3(vertex.position) * mesh_asset.rcp_quantization_scale + meshlet.position_offset;
		
		float4 clip_space_position      = TransformModelToClipSpace(vertex_position, model_to_world,      scene.world_to_view,      scene.view_to_clip_coef);
		float4 prev_clip_space_position = TransformModelToClipSpace(vertex_position, prev_model_to_world, scene.prev_world_to_view, scene.prev_view_to_clip_coef);
		
		InputPS output;
		output.position = clip_space_position;
		output.texcoord = vertex.texcoord;
		output.normal   = QuatMul(model_to_world.rotation, DecodeOctahedralMap(DecodeR16G16_SNORM(vertex.normal)));
		output.curr_position = clip_space_position.xyw;
		output.prev_position = prev_clip_space_position.xyw;
		
		output.position.xy += scene.jitter_offset_ndc * output.position.w;
		
		result_vertices[thread_index] = output;
	}
	
	if (thread_index < meshlet.triangle_count) {
		compile_const uint triangle_size_bytes = 3;
		uint load_offset = index_buffer_offset + thread_index * triangle_size_bytes;
		uint2 packed_indices = mesh_asset_buffer.Load<uint2>(load_offset & ~0x3);
		
		uint indices = uint((u64(packed_indices.x) | (u64(packed_indices.y) << 32)) >> ((load_offset & 0x3) * 8));
		result_indices[thread_index] = uint3(indices >> 0, indices >> 8, indices >> 16) & 0xFF;
		result_primitives[thread_index].meshlet_index  = meshlet_header_offset >> 4;
		result_primitives[thread_index].material_index = mesh_entity.material_asset_index;
	}
}
#endif // defined(MESH_SHADER)

#if defined(PIXEL_SHADER)
struct OutputPS {
	float4 color : SV_Target0;
	float2 motion_vectors : SV_Target1;
};

OutputPS MainPS(InputPS input, InputPrimitivePS primitive_input, float3 bary : SV_Barycentrics) {
	float3 normal_color   = input.normal * 0.5 + 0.5;
	float3 texcoord_color = float3(input.texcoord, 0.0);
	
	float3 meshlet_color = (primitive_input.meshlet_index & 0x1) ? normal_color : texcoord_color;
	float  wireframe     = BarycentricWireframe(bary, ddx(bary), ddy(bary));
	
	if (primitive_input.material_index != u32_max) {
		GpuMaterialTextureData material = material_texture_data[primitive_input.material_index];
		if (material.albedo != u32_max) {
			Texture2D<float3> texture = ResourceDescriptorHeap[material.albedo];
			meshlet_color = texture.Sample(sampler_linear_wrap, input.texcoord);
			wireframe = 1.0;
		}
	}
	
	OutputPS output;
	output.color = float4(meshlet_color * wireframe, 1.0);
	output.motion_vectors = NdcToScreenUvDirection((input.prev_position.xy / input.prev_position.z) - (input.curr_position.xy / input.curr_position.z));
	
	return output;
}
#endif // defined(PIXEL_SHADER)
