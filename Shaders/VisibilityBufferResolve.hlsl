#include "Basic.hlsl"
#include "MaterialSampling.hlsl"
#include "MeshletVertexDecoding.hlsl"

compile_const u32 thread_group_size = 16;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * scene.inv_render_target_size;
	
	uint scene_primitive_id = visibility_buffer[thread_id];
	if (scene_primitive_id == 0) return;
	
	u32 visible_meshlet_index = (scene_primitive_id >> 7) - 1;
	uint2 meshlet_instance    = visible_meshlets[visible_meshlet_index];
	u32 meshlet_header_offset = meshlet_instance.x;
	u32 mesh_entity_index     = meshlet_instance.y;
	u32 triangle_index        = (scene_primitive_id & 0x7F);
	
	GpuTransform   model_to_world = mesh_transforms[mesh_entity_index];
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
	
	uint3 indices = LoadMeshletIndexBuffer(mesh_asset_buffer, offsets.index_buffer_offset, triangle_index);
	MeshletVertex v0 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.x);
	MeshletVertex v1 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.y);
	MeshletVertex v2 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.z);
	
	float3 barycentrics;
	barycentrics.yz = 1.0 / 3.0; // TODO: Barycentrics.
	barycentrics.x  = 1.0 - barycentrics.y - barycentrics.z;
	
	float3 model_space_normal  = DecodeAndInterpolateUnitVector(barycentrics, v0.normal,  v1.normal,  v2.normal);
	float3 model_space_tangent = DecodeAndInterpolateUnitVector(barycentrics, v0.tangent, v1.tangent, v2.tangent);
	
	TexcoordStream texcoord_stream;
	texcoord_stream.texcoord = BarycentricInterpolation<float2>(barycentrics, v0.texcoord, v1.texcoord, v2.texcoord);
	texcoord_stream.texcoord_ddx = 0.0; // TODO: Barycentric derivatives.
	texcoord_stream.texcoord_ddy = 0.0;
	
	MaterialProperties properties = SampleMaterial(mesh_entity.material_asset_index, texcoord_stream);
	
	float3x3 tangent_to_model = ComputeTangentToOtherSpace(model_space_tangent, model_space_normal);
	float3x3 tangent_to_world = mul(QuatToRotationMatrix(model_to_world.rotation), tangent_to_model);
	float3 world_space_normal = normalize(mul(tangent_to_world, properties.normal));
	
	float3 meshlet_color = properties.albedo * max(world_space_normal.z * 0.5 + 0.5, 0.0);
	scene_radiance[thread_id] = float4(meshlet_color * scene.exposure_estimate, 1.0);
}
