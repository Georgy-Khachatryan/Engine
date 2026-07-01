#ifndef GEOMETRYSAMPLING_HLSL
#define GEOMETRYSAMPLING_HLSL
#include "Basic.hlsl"
#include "MeshletVertexDecoding.hlsl"
#include "MaterialSampling.hlsl"

MaterialProperties SampleMaterialFromHitResult(
	u32 meshlet_header_offset,
	u32 mesh_entity_index,
	u32 triangle_index,
	float2 barycentrics_yz,
	bool is_front_face,
	float texcoord_grad = 0.0
) {
	GpuTransform   model_to_world = mesh_transforms[mesh_entity_index];
	GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
	
	uint3 indices = LoadMeshletIndexBuffer(mesh_asset_buffer, offsets.index_buffer_offset, triangle_index);
	MeshletVertex v0 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.x);
	MeshletVertex v1 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.y);
	MeshletVertex v2 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.z);
	
	float3 barycentrics;
	barycentrics.yz = barycentrics_yz;
	barycentrics.x  = 1.0 - barycentrics.y - barycentrics.z;
	
	float3 model_space_normal  = DecodeAndInterpolateUnitVector(barycentrics, v0.normal,  v1.normal,  v2.normal);
	float3 model_space_tangent = DecodeAndInterpolateUnitVector(barycentrics, v0.tangent, v1.tangent, v2.tangent);
	
	TexcoordStream texcoord_stream;
	texcoord_stream.texcoord     = BarycentricInterpolation<float2>(barycentrics, v0.texcoord, v1.texcoord, v2.texcoord);
	texcoord_stream.texcoord_ddx = texcoord_grad;
	texcoord_stream.texcoord_ddy = texcoord_grad;
	
	MaterialProperties properties = SampleMaterial(mesh_entity.material_asset_index, texcoord_stream);
	
	float3x3 tangent_to_model = ComputeTangentToOtherSpace(model_space_tangent, model_space_normal);
	float3x3 tangent_to_world = mul(QuatToRotationMatrix(model_to_world.rotation), tangent_to_model);
	properties.normal = (float16x3)normalize(mul(tangent_to_world, properties.normal));
	
	if (is_front_face == false) {
		properties.normal = -properties.normal;
	}
	
	return properties;
}

#endif // GEOMETRYSAMPLING_HLSL
