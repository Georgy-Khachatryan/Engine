#include "Basic.hlsl"
#include "MeshletVertexDecoding.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD;
	float3 world_space_normal  : NORMAL;
	float3 world_space_tangent : TANGENT;
	
	float3 curr_position : CURR_POSITION;
	float3 prev_position : PREV_POSITION;
};

struct InputPrimitivePS {
	uint visualization_id : VISUALIZATION_ID;
	uint material_index   : MATERIAL_INDEX;
};

#define VISUALIZATION_TYPE 3

#if defined(MESH_SHADER)
[OutputTopology("triangle")]
[ThreadGroupSize(128, 1, 1)]
void MainMS(
	uint group_id : SV_GroupID,
	uint thread_index : SV_GroupIndex,
	out vertices InputPS result_vertices[128],
	out indices uint3 result_indices[128],
	out primitives InputPrimitivePS result_primitives[128]) {
	
	if (constants.pass == MeshletCullingPass::Disocclusion) {
		group_id += indirect_arguments[MeshletCullingIndirectArgumentsLayout::DispatchMesh].x;
	}
	uint2 meshlet_instance = visible_meshlets[group_id]; 
	
	uint mesh_entity_index = meshlet_instance.y;
	uint meshlet_header_offset = meshlet_instance.x;
	
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	GpuMeshAssetData mesh_asset = mesh_asset_data[mesh_entity.mesh_asset_index];
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	
	SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);
	
	MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
	
	if (thread_index < meshlet.vertex_count) {
		MeshletVertex vertex = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, thread_index);
		float3 vertex_position = DecodeMeshletVertexPosition(vertex.position, mesh_asset, meshlet);
		
		float4 clip_space_position      = TransformModelToClipSpace(vertex_position, model_to_world,      scene.world_to_view,      scene.view_to_clip_coef);
		float4 prev_clip_space_position = TransformModelToClipSpace(vertex_position, prev_model_to_world, scene.prev_world_to_view, scene.prev_view_to_clip_coef);
		
		InputPS output;
		output.position = clip_space_position;
		output.texcoord = vertex.texcoord;
		output.world_space_normal  = QuatMul(model_to_world.rotation, DecodeOctahedralMap(DecodeR16G16_SNORM(vertex.normal)));
		output.world_space_tangent = QuatMul(model_to_world.rotation, DecodeOctahedralMap(DecodeR16G16_SNORM(vertex.tangent)));
		output.curr_position = clip_space_position.xyw;
		output.prev_position = prev_clip_space_position.xyw;
		
		output.position.xy += scene.jitter_offset_ndc * output.position.w;
		
		result_vertices[thread_index] = output;
	}
	
	if (thread_index < meshlet.triangle_count) {
		result_indices[thread_index] = LoadMeshletIndexBuffer(mesh_asset_buffer, offsets.index_buffer_offset, thread_index);
		
		result_primitives[thread_index].visualization_id = VISUALIZATION_TYPE == 0 ? (meshlet_header_offset >> 4) : VISUALIZATION_TYPE == 1 ? meshlet.level_of_detail_index : VISUALIZATION_TYPE == 2 ? mesh_entity_index : 0;
		result_primitives[thread_index].material_index   = mesh_entity.material_asset_index;
	}
}
#endif // defined(MESH_SHADER)

#if defined(PIXEL_SHADER)
#include "MaterialSampling.hlsl"

struct OutputPS {
	float4 color : SV_Target0;
	float2 motion_vectors : SV_Target1;
};

OutputPS MainPS(InputPS input, InputPrimitivePS primitive_input, float3 bary : SV_Barycentrics) {
#if (VISUALIZATION_TYPE == 1)
	// float3 meshlet_color = ViridisHeatMap(Pow2(1.0 - saturate(primitive_input.visualization_id * rcp(15.0))));
	float3 meshlet_color = PlasmaHeatMap(Pow2(1.0 - saturate(primitive_input.visualization_id * rcp(15.0))));
#else // (VISUALIZATION_TYPE != 0)
	float3 meshlet_color = DecodeSRGB(RandomColor(primitive_input.visualization_id));
#endif // (VISUALIZATION_TYPE != 0)
	
#if (VISUALIZATION_TYPE == 3)
	TexcoordStream texcoord_stream;
	texcoord_stream.texcoord = input.texcoord;
	MaterialProperties properties = SampleMaterial(primitive_input.material_index, texcoord_stream);
	
	float3x3 tangent_to_world = ComputeTangentToOtherSpace(input.world_space_tangent, input.world_space_normal);
	float3 world_space_normal = mul(tangent_to_world, properties.normal);
	
	meshlet_color = properties.albedo * max(world_space_normal.z * 0.5 + 0.5, 0.0);
#else // (VISUALIZATION_TYPE != 3)
	meshlet_color *= BarycentricWireframe(bary, ddx(bary), ddy(bary)) * 0.5 + 0.5;
#endif // (VISUALIZATION_TYPE == 3)
	
	OutputPS output;
	output.color = float4(meshlet_color, 1.0);
	output.motion_vectors = NdcToScreenUvDirection((input.prev_position.xy / input.prev_position.z) - (input.curr_position.xy / input.curr_position.z));
	
	return output;
}
#endif // defined(PIXEL_SHADER)
