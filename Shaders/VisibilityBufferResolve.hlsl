#include "Basic.hlsl"
#include "MaterialSampling.hlsl"
#include "MeshletVertexDecoding.hlsl"

compile_const u32 thread_group_size = 16;

float4 TransformModelToClipSpace(MeshletVertex vertex, GpuTransform model_to_world, GpuMeshAssetData mesh_asset, MeshletHeader meshlet, float3x4 world_to_view, float4 view_to_clip_coef) {
	float3 model_space_position = DecodeMeshletVertexPosition(vertex.position, mesh_asset, meshlet);
	return TransformModelToClipSpace(model_space_position, model_to_world, world_to_view, view_to_clip_coef);
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	uint scene_primitive_id = visibility_buffer[thread_id];
	if (scene_primitive_id == 0) return;
	
	u32 visible_meshlet_index = (scene_primitive_id >> 7) - 1;
	uint2 meshlet_instance    = visible_meshlets[visible_meshlet_index];
	u32 meshlet_header_offset = meshlet_instance.x;
	u32 mesh_entity_index     = meshlet_instance.y;
	u32 triangle_index        = (scene_primitive_id & 0x7F);
	
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	GpuMeshAssetData  mesh_asset  = mesh_asset_data[mesh_entity.mesh_asset_index];
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
	
	uint3 indices = LoadMeshletIndexBuffer(mesh_asset_buffer, offsets.index_buffer_offset, triangle_index);
	MeshletVertex v0 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.x);
	MeshletVertex v1 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.y);
	MeshletVertex v2 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.z);
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	float4 p0 = TransformModelToClipSpace(v0, model_to_world, mesh_asset, meshlet, scene.world_to_view, scene.view_to_clip_coef);
	float4 p1 = TransformModelToClipSpace(v1, model_to_world, mesh_asset, meshlet, scene.world_to_view, scene.view_to_clip_coef);
	float4 p2 = TransformModelToClipSpace(v2, model_to_world, mesh_asset, meshlet, scene.world_to_view, scene.view_to_clip_coef);
	
	BarycentricsWithDerivatives b = ComputeBarycentricsWithDerivatives(p0, p1, p2, ScreenUvToNdc(thread_uv), scene.inv_render_target_size);
	
	GpuTransform prev_model_to_world = prev_mesh_transforms[mesh_entity_index];
	float4 prev_p0 = TransformModelToClipSpace(v0, prev_model_to_world, mesh_asset, meshlet, scene.prev_world_to_view, scene.prev_view_to_clip_coef);
	float4 prev_p1 = TransformModelToClipSpace(v1, prev_model_to_world, mesh_asset, meshlet, scene.prev_world_to_view, scene.prev_view_to_clip_coef);
	float4 prev_p2 = TransformModelToClipSpace(v2, prev_model_to_world, mesh_asset, meshlet, scene.prev_world_to_view, scene.prev_view_to_clip_coef);
	float3 prev_p  = BarycentricInterpolation(b.barycentrics, prev_p0.xyw, prev_p1.xyw, prev_p2.xyw);
	motion_vectors[thread_id] = NdcToScreenUv(prev_p.xy / prev_p.z) - thread_uv;
	
	float3 model_space_normal  = DecodeAndInterpolateUnitVector(b.barycentrics, v0.normal,  v1.normal,  v2.normal);
	float3 model_space_tangent = DecodeAndInterpolateUnitVector(b.barycentrics, v0.tangent, v1.tangent, v2.tangent);
	
	TexcoordStream texcoord_stream;
	texcoord_stream.texcoord     = BarycentricInterpolation<float2>(b.barycentrics,     v0.texcoord, v1.texcoord, v2.texcoord);
	texcoord_stream.texcoord_ddx = BarycentricInterpolation<float2>(b.barycentrics_ddx, v0.texcoord, v1.texcoord, v2.texcoord);
	texcoord_stream.texcoord_ddy = BarycentricInterpolation<float2>(b.barycentrics_ddy, v0.texcoord, v1.texcoord, v2.texcoord);
	
	MaterialProperties properties = SampleMaterial(mesh_entity.material_asset_index, texcoord_stream);
	gb_albedo_metalness[thread_id] = EncodeSRGB(float4(properties.albedo, properties.metalness));
	
	float3x3 tangent_to_model = ComputeTangentToOtherSpace(model_space_tangent, model_space_normal);
	float3x3 tangent_to_world = mul(QuatToRotationMatrix(model_to_world.rotation), tangent_to_model);
	float3 world_space_normal = normalize(mul(tangent_to_world, properties.normal));
	
	float2 octahedral_normal = EncodeHemiOctahedralMap(world_space_normal);
	gb_normal_roughness[thread_id] = float4(octahedral_normal * 0.5 + 0.5, properties.roughness, world_space_normal.z >= 0.0 ? 1.0 : 0.0);
	
	float3 meshlet_color = properties.albedo * max(world_space_normal.z * 0.5 + 0.5, 0.0);
	scene_radiance[thread_id] = float4(meshlet_color * scene.exposure_estimate, 1.0);
}
