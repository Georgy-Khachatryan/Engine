#include "Basic.hlsl"


//
// Based on "Sampling Visible GGX Normals with Spherical Caps" by Jonathan Dupuy and Anis Benyoub.
//
// Sampling the visible hemisphere as half vectors.
//
float3 SampleVndfHemisphere(float2 u, float3 wo) {
	// Sample a spherical cap in (-wo.z, 1].
	float phi = 2.0 * PI * u.x;
	
	float z = mad((1.0 - u.y), (1.0 + wo.z), -wo.z);
	float sin_theta = sqrt(saturate(1.0 - z * z));
	
	float x = sin_theta * cos(phi);
	float y = sin_theta * sin(phi);
	
	// Compute halfway direction, return without normalization (as this is done later).
	return float3(x, y, z) + wo;
}

// Trowbridge Reitz (GGX) VNDF sampling. PDF = (G_1 * D) / (4.0 * cos_theta_o)
float3 SampleTrowbridgeReitzVNDF(float2 u, float3 wo, float2 alpha) {
	// Warp to the hemisphere configuration.
	float3 wo_standard = normalize(float3(wo.xy * alpha, wo.z));
	
	// Sample the hemisphere.
	float3 wm_standard = SampleVndfHemisphere(u, wo_standard);
	
	// Warp the microfacet normal back to the ellipsoid configuration.
	return normalize(float3(wm_standard.xy * alpha, wm_standard.z));
}

float3 FresnelSchlick(float3 f0, float cos_theta) {
	return f0 + (1.0 - f0) * Pow5(1.0 - cos_theta);
}

float SmithVisibilityLambda(float cos_theta, float alpha_square) {
	float tan_theta_square = rcp(Pow2(cos_theta)) - 1.0;
	return (sqrt(alpha_square * tan_theta_square + 1.0) - 1.0) * 0.5;
}

// Smith masking function.
float SmithVisibilityG1(float3 w, float alpha_square) {
	return rcp(1.0 + SmithVisibilityLambda(w.z, alpha_square));
}

// Smith masking-shadowing function.
float SmithVisibilityG(float3 wo, float3 wi, float alpha_square) {
	return rcp(1.0 + SmithVisibilityLambda(wo.z, alpha_square) + SmithVisibilityLambda(wi.z, alpha_square)); // Height correlated.
	// return G1(wo, alpha_square) * G1(wi, alpha_square); // Uncorrelated.
}

compile_const u32 energy_compensation_lut_size = 32u;
compile_const float energy_compensation_lut_cos_theta_bias = (1.0 / 1024.0);


// Result has exclusive upper bound, i.e. the range is [0, 1).
float2 ComputeRandomUnorm16x2(inout uint hash) {
	float2 result = float2(hash & 0xFFFF, (hash >> 16) & 0xFFFF) * rcp(0x10000);
	hash = WyHash32(hash, 0);
	return result;
}


#if defined(REFERENCE_PATH_TRACER)
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
	uint2 thread_id = group_id * uint2(8, 4) + MortonDecode(thread_index);
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), scene.path_tracer_accumulated_frame_count);
	float2 thread_uv = (thread_id + ComputeRandomUnorm16x2(hash)) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = 1024.0;
	
	float3 radiance   = 0.0;
	float3 throughput = 1.0;
	uint max_path_length = 512 + 2;
	
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
			
			if (ray_query.CommittedTriangleFrontFace() == false) {
				world_space_normal = -world_space_normal;
			}
			ray_desc.Origin += ray_desc.Direction * ray_query.CommittedRayT() + world_space_normal * (1.0 / 1024.0);
			
			{
				float  roughness    = properties.roughness;
				float3 f0           = properties.albedo;
				float  alpha        = Pow2(roughness);
				float  alpha_square = Pow2(alpha);
				
				float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
				float3x3 tangent_to_world = transpose(world_to_tangent);
				
				float3 wo = mul(world_to_tangent, -ray_desc.Direction);
				float3 wh = SampleTrowbridgeReitzVNDF(ComputeRandomUnorm16x2(hash), wo, alpha);
				float3 wi = reflect(-wo, wh);
				
				ray_desc.Direction = mul(tangent_to_world, wi);
				
				if ((wi.z * wo.z) > 0.0) {
					float3 specular_fresnel = FresnelSchlick(f0, dot(wo, wh));
					
					// Based on "Practical multiple scattering compensation for microfacet models" by Emmanuel Turquin.
					float2 energy_compensation_lut_parameters = saturate(float2(wo.z + energy_compensation_lut_cos_theta_bias, roughness));
					float2 energy_compensation_lut_uv = LutParametersToUv(energy_compensation_lut_parameters, energy_compensation_lut_size);
					float  single_scattering_energy   = ggx_conductor_energy_lut.SampleLevel(sampler_linear_clamp, energy_compensation_lut_uv, 0);
					float3 energy_compensation        = (1.0 + specular_fresnel * (1.0 - single_scattering_energy) / single_scattering_energy);
					
					throughput *= specular_fresnel * energy_compensation * (SmithVisibilityG(wo, wi, alpha_square) / SmithVisibilityG1(wo, alpha_square));
				} else {
					i = max_path_length;
				}
			}
		} else {
			radiance += throughput;
			i = max_path_length;
		}
	}
	
	float3 old_accumulated_radiance = max(path_tracer_radiance[thread_id].xyz, 0.0);
	float3 new_accumulated_radiance = (old_accumulated_radiance * (scene.path_tracer_accumulated_frame_count - 1) + radiance) / (float)scene.path_tracer_accumulated_frame_count;
	path_tracer_radiance[thread_id] = float4(new_accumulated_radiance, 1.0);
	
	uint reference_path_tracer_min_x = (uint)(scene.render_target_size.x * scene.reference_path_tracer_percent);
	if (thread_id.x < reference_path_tracer_min_x) {
		scene_radiance[thread_id] = float4(new_accumulated_radiance, 1.0);
	}
}
#endif // defined(REFERENCE_PATH_TRACER)


#if defined(ENERGY_COMPENSATION_LUT)
compile_const u32 thread_group_size   = 1024;
compile_const u32 sample_grid_size_xy = 128;
compile_const u32 sample_count        = sample_grid_size_xy * sample_grid_size_xy;
compile_const u32 samples_per_thread  = sample_count / thread_group_size;

groupshared float gs_single_scattering_energy[64];

[ThreadGroupSize(thread_group_size, 1, 1)][WaveSize(16, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	float2 group_uv = group_id * (1.0 / (energy_compensation_lut_size - 1));
	
	float roughness    = group_uv.y;
	float alpha        = Pow2(roughness);
	float alpha_square = Pow2(alpha);
	float cos_theta    = saturate(group_uv.x + energy_compensation_lut_cos_theta_bias);
	
	float3 wo = float3(sqrt(1.0 - Pow2(cos_theta)), 0.0, cos_theta);
	
	precise float single_scattering_energy = 0.0;
	for (u32 i = 0; i < samples_per_thread; i += 1) {
		u32 sample_index = thread_index * samples_per_thread + i;
		
		// Sample UV have exclusive upper bound, i.e. the range is [0, 1).
		float2 sample_uv = uint2(sample_index % sample_grid_size_xy, sample_index / sample_grid_size_xy) * (1.0 / sample_grid_size_xy);
		
		float3 wh = SampleTrowbridgeReitzVNDF(sample_uv, wo, alpha);
		float3 wi = reflect(-wo, wh);
		
		if ((wi.z * wo.z) > 0.0) {
			single_scattering_energy += SmithVisibilityG(wo, wi, alpha_square) / SmithVisibilityG1(wo, alpha_square);
		}
	}
	
	float wave_single_scattering_energy = WaveActiveSum(single_scattering_energy);
	if (WaveIsFirstLane()) {
		gs_single_scattering_energy[thread_index / WaveGetLaneCount()] = wave_single_scattering_energy;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		float group_single_scattering_energy = wave_single_scattering_energy;
		
		u32 wave_count = thread_group_size / WaveGetLaneCount();
		for (u32 i = 1; i < wave_count; i += 1) {
			group_single_scattering_energy += gs_single_scattering_energy[i];
		}
		
		ggx_conductor_energy_lut[group_id] = group_single_scattering_energy / sample_count;
	}
}
#endif // defined(ENERGY_COMPENSATION_LUT)
