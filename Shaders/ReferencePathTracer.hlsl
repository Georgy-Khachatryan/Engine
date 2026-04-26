#include "Basic.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"
#include "MaterialSampling.hlsl"
#include "MeshletVertexDecoding.hlsl"

template<typename T>
T BarycentricInterpolation(float3 barycentrics, T v0, T v1, T v2) {
	return v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
}

float3 DecodeAndInterpolateUnitVector(float3 barycentrics, u16x2 v0, u16x2 v1, u16x2 v2) {
	float3 n0 = DecodeOctahedralMap(DecodeR16G16_SNORM(v0));
	float3 n1 = DecodeOctahedralMap(DecodeR16G16_SNORM(v1));
	float3 n2 = DecodeOctahedralMap(DecodeR16G16_SNORM(v2));
	return BarycentricInterpolation(barycentrics, n0, n1, n2);
}

[ThreadGroupSize(32, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * uint2(8, 4) + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = 1024.0;
	
	float3 radiance   = 0.0;
	float3 throughput = 1.0;
	uint max_path_length = 4;
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), scene.frame_index);
	
	[loop]
	for (uint i = 0; i < max_path_length; i += 1) {
		RayQuery<
			RAY_FLAG_CULL_NON_OPAQUE |
			RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
			// RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
			// RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
			RAY_FLAG_NONE
		> ray_query;
		
		ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
		
		while (ray_query.Proceed()) {
			
		}
		
		if (ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
			u32 meshlet_header_offset = NvRtGetCommittedClusterID(ray_query);
			u32 mesh_entity_index     = ray_query.CommittedInstanceID();
			u32 triangle_index        = ray_query.CommittedPrimitiveIndex();
			
			GpuTransform   model_to_world = mesh_transforms[mesh_entity_index];
			GpuMeshEntityData mesh_entity = mesh_entity_data[mesh_entity_index];
			
			MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
			MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
			
			uint3 indices = LoadMeshletIndexBuffer(mesh_asset_buffer, offsets.index_buffer_offset, triangle_index);
			MeshletVertex v0 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.x);
			MeshletVertex v1 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.y);
			MeshletVertex v2 = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, indices.z);
			
			float3 barycentrics;
			barycentrics.yz = ray_query.CommittedTriangleBarycentrics();
			barycentrics.x  = 1.0 - barycentrics.y - barycentrics.z;
			
			float3 model_space_normal  = DecodeAndInterpolateUnitVector(barycentrics, v0.normal,  v1.normal,  v2.normal);
			float3 model_space_tangent = DecodeAndInterpolateUnitVector(barycentrics, v0.tangent, v1.tangent, v2.tangent);
			
			TexcoordStream texcoord_stream;
			texcoord_stream.texcoord = BarycentricInterpolation<float2>(barycentrics, v0.texcoord, v1.texcoord, v2.texcoord);
			texcoord_stream.texcoord_ddx = 0.0;
			texcoord_stream.texcoord_ddy = 0.0;
			
			MaterialProperties properties = SampleMaterial(mesh_entity.material_asset_index, texcoord_stream);
			
			float3x3 tangent_to_model = ComputeTangentToOtherSpace(model_space_tangent, model_space_normal);
			float3x3 tangent_to_world = mul(QuatToRotationMatrix(model_to_world.rotation), tangent_to_model);
			float3 world_space_normal = normalize(mul(tangent_to_world, properties.normal));
			
			if (ray_query.CandidateTriangleFrontFace() == false) {
				world_space_normal = -world_space_normal;
			}
			
			throughput *= properties.albedo;
			
			ray_desc.Origin += ray_desc.Direction * ray_query.CommittedRayT() + world_space_normal * (1.0 / 1024.0);
			
			float3 tangent_space_direction = CosineWeightedHemisphereMapping((float2(hash & 0xFF, (hash >> 8) & 0xFF)) * rcp(0xFF));
			hash = WyHash32(hash, 0);
			
			ray_desc.Direction = normalize(mul(transpose(BuildOrthonormalBasis(world_space_normal)), tangent_space_direction));
		} else {
			radiance += throughput * max(ray_desc.Direction.z, 0.0);
			i = max_path_length;
		}
	}
	
	uint reference_path_tracer_min_x = (uint)(scene.render_target_size.x * scene.reference_path_tracer_percent);
	if (thread_id.x < reference_path_tracer_min_x) {
		scene_radiance[thread_id] = float4(radiance, 1.0);
	}
}
