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

// Trowbridge Reitz (GGX) VNDF sampling. PDF = (SmithVisibilityG1(cos_theta_o) * TrowbridgeReitzNDF(cos_theta_m)) / (4.0 * cos_theta_o)
float3 SampleTrowbridgeReitzVNDF(float2 u, float3 wo, float2 alpha) {
	// Warp to the hemisphere configuration.
	float3 wo_standard = normalize(float3(wo.xy * alpha, wo.z));
	
	// Sample the hemisphere.
	float3 wm_standard = SampleVndfHemisphere(u, wo_standard);
	
	// Warp the microfacet normal back to the ellipsoid configuration.
	return normalize(float3(wm_standard.xy * alpha, wm_standard.z));
}

float TrowbridgeReitzNDF(float cos_theta_m, float alpha_square) {
	return alpha_square * rcp(PI * Pow2(1.0 + Pow2(cos_theta_m) * (alpha_square - 1.0)));
}

template<typename T>
T FresnelSchlick(T f0, float cos_theta) {
	return f0 + (1.0 - f0) * Pow5(1.0 - cos_theta);
}

float3 FresnelConductor(float3 f0, float cos_theta) {
	return FresnelSchlick<float3>(f0, cos_theta);
}

float FresnelDielectric(float f0, float cos_theta) {
	return FresnelSchlick<float>(f0, cos_theta);
}

float SmithVisibilityLambda(float cos_theta, float alpha_square) {
	float tan_theta_square = rcp(Pow2(cos_theta)) - 1.0;
	return (sqrt(alpha_square * tan_theta_square + 1.0) - 1.0) * 0.5;
}

// Smith masking function.
float SmithVisibilityG1(float cos_theta, float alpha_square) {
	return rcp(1.0 + SmithVisibilityLambda(cos_theta, alpha_square));
}

// Smith masking-shadowing function.
float SmithVisibilityG(float cos_theta_o, float cos_theta_i, float alpha_square) {
	return rcp(1.0 + SmithVisibilityLambda(cos_theta_o, alpha_square) + SmithVisibilityLambda(cos_theta_i, alpha_square)); // Height correlated.
	// return G1(wo, alpha_square) * G1(wi, alpha_square); // Uncorrelated.
}

compile_const u32 energy_compensation_lut_size = 32u;
compile_const float energy_compensation_lut_cos_theta_bias = (1.0 / 1024.0);
compile_const float dielectric_f0 = 0.04;


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
#include "AtmosphereSampling.hlsl"

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

float TraceShadowRay(float3 ray_origin, float3 ray_direction) {
	RayQuery<
		RAY_FLAG_CULL_NON_OPAQUE |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
		// RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_NONE
	> ray_query;
	
	RayDesc ray_desc;
	ray_desc.Origin    = ray_origin;
	ray_desc.Direction = ray_direction;
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = 1024.0;
	
	ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
	
	while (ray_query.Proceed()) {
		
	}
	
	return ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

// Based on "Practical multiple scattering compensation for microfacet models" by Emmanuel Turquin.
float2 SampleGgxSingleScatteringEnergyLUT(float cos_theta, float roughness) {
	float2 energy_compensation_lut_parameters = saturate(float2(cos_theta + energy_compensation_lut_cos_theta_bias, roughness));
	float2 energy_compensation_lut_uv = LutParametersToUv(energy_compensation_lut_parameters, energy_compensation_lut_size);
	
	return ggx_single_scattering_energy_lut.SampleLevel(sampler_linear_clamp, energy_compensation_lut_uv, 0);
}

float3 ComputeConductorBrdfEnergyCompensation(float2 single_scattering_energy, float3 specular_fresnel) {
	return (1.0 + specular_fresnel * (1.0 - single_scattering_energy.x) / single_scattering_energy.x);
}

float ComputeDielectricBrdfEnergyCompensation(float2 single_scattering_energy) {
	return (1.0 / single_scattering_energy.y);
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
			
			
			float  metalness    = properties.metalness;
			float  roughness    = properties.roughness;
			float3 metallic_f0  = properties.albedo;
			float  alpha        = Pow2(roughness);
			float  alpha_square = Pow2(alpha);
			float3 diffuse_albedo = properties.albedo;
			
			{
				float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
				float3x3 tangent_to_world = transpose(world_to_tangent);
				
				float3 light_direction = atmosphere.world_space_sun_direction;
				float3 wi = mul(world_to_tangent, light_direction);
				float3 wo = mul(world_to_tangent, -ray_desc.Direction);
				float3 wh = normalize(wo + wi);
				
				float3 light_irradiance = atmosphere.sun_irradiance * SampleTransmittanceLUT(atmosphere, transmittance_lut, ray_desc.Origin);
				float shadow = TraceShadowRay(ray_desc.Origin, light_direction);
				float3 shadowed_light_irradiance = (throughput * light_irradiance) * (shadow * saturate(wi.z));
				
				float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(wo.z, roughness);
				
				if (metalness != 0.0 && (wi.z * wo.z) > 0.0) {
					float3 specular_fresnel = FresnelConductor(metallic_f0, dot(wi, wh));
					float3 energy_compensation = ComputeConductorBrdfEnergyCompensation(single_scattering_energy, specular_fresnel) * metalness;
					
					float3 specular_brdf = specular_fresnel * SmithVisibilityG(wo.z, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * wo.z);
					
					radiance += shadowed_light_irradiance * (energy_compensation * specular_brdf);
				}
				
				if (metalness != 1.0 && (wi.z * wo.z) > 0.0) {
					float specular_fresnel = FresnelDielectric(dielectric_f0, dot(wi, wh));
					float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy) * (1.0 - metalness);
					
					float specular_brdf = specular_fresnel * SmithVisibilityG(wo.z, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * wo.z);
					float3 diffuse_brdf = diffuse_albedo * ((1.0 - specular_fresnel) * rcp(PI));
					
					radiance += shadowed_light_irradiance * (energy_compensation * specular_brdf);
					radiance += shadowed_light_irradiance * (energy_compensation * diffuse_brdf);
				}
			}
			
			{
				float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
				float3x3 tangent_to_world = transpose(world_to_tangent);
				
				float3 wo = mul(world_to_tangent, -ray_desc.Direction);
				float3 wh = SampleTrowbridgeReitzVNDF(ComputeRandomUnorm16x2(hash), wo, alpha);
				float3 wi = reflect(-wo, wh);
				
				if ((wi.z * wo.z) > 0.0) {
					float2 mis_thresholds = ComputeRandomUnorm16x2(hash);
					
					float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(wo.z, roughness);
					
					if (mis_thresholds.x < metalness) {
						float3 specular_fresnel = FresnelConductor(metallic_f0, dot(wo, wh));
						float3 energy_compensation = ComputeConductorBrdfEnergyCompensation(single_scattering_energy, specular_fresnel);
						
						ray_desc.Direction = mul(tangent_to_world, wi);
						
						throughput *= specular_fresnel * energy_compensation * (SmithVisibilityG(wo.z, wi.z, alpha_square) / SmithVisibilityG1(wo.z, alpha_square));
					} else {
						float specular_fresnel = FresnelDielectric(dielectric_f0, dot(wo, wh));
						float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy);
						
						float mis_specular_weight = specular_fresnel;
						float mis_diffuse_weight  = dot(diffuse_albedo, rec709_luminance_coefficients) * (1.0 - mis_specular_weight);
						float normalized_mis_specular_weight = mis_specular_weight / (mis_specular_weight + mis_diffuse_weight);
						float normalized_mis_diffuse_weight  = 1.0 - normalized_mis_specular_weight;
						
						if (mis_thresholds.y < normalized_mis_specular_weight) {
							ray_desc.Direction = normalize(mul(tangent_to_world, wi));
							
							throughput *= specular_fresnel * (energy_compensation * (SmithVisibilityG(wo.z, wi.z, alpha_square) / SmithVisibilityG1(wo.z, alpha_square)) / normalized_mis_specular_weight);
						} else {
							ray_desc.Direction = normalize(mul(tangent_to_world, CosineWeightedHemisphereMapping(ComputeRandomUnorm16x2(hash))));
							
							throughput *= diffuse_albedo * (energy_compensation * (1.0 - specular_fresnel) / normalized_mis_diffuse_weight);
						}
					}
				} else {
					i = max_path_length;
				}
			}
		} else {
			float3 sky_radiance = SampleSkyPanoramaLUT(atmosphere, sky_panorama_lut, transmittance_lut, scene.world_space_camera_position, ray_desc.Direction, i == 0);
			
			radiance += throughput * sky_radiance;
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

groupshared float2 gs_single_scattering_energy[64];

[ThreadGroupSize(thread_group_size, 1, 1)][WaveSize(16, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	float2 group_uv = group_id * (1.0 / (energy_compensation_lut_size - 1));
	
	float roughness    = group_uv.y;
	float alpha        = Pow2(roughness);
	float alpha_square = Pow2(alpha);
	float cos_theta    = saturate(group_uv.x + energy_compensation_lut_cos_theta_bias);
	
	float3 wo = float3(sqrt(1.0 - Pow2(cos_theta)), 0.0, cos_theta);
	
	float2 single_scattering_energy = 0.0;
	for (u32 i = 0; i < samples_per_thread; i += 1) {
		u32 sample_index = thread_index * samples_per_thread + i;
		
		// Sample UV have exclusive upper bound, i.e. the range is [0, 1).
		float2 sample_uv = uint2(sample_index % sample_grid_size_xy, sample_index / sample_grid_size_xy) * (1.0 / sample_grid_size_xy);
		
		float3 wh = SampleTrowbridgeReitzVNDF(sample_uv, wo, alpha);
		float3 wi = reflect(-wo, wh);
		
		if ((wi.z * wo.z) > 0.0) {
			float specular_sample = SmithVisibilityG(wo.z, wi.z, alpha_square) / SmithVisibilityG1(wo.z, alpha_square);
			single_scattering_energy.x += specular_sample; // Conductor
			
			float fresnel = FresnelSchlick(dielectric_f0, dot(wo, wh));
			single_scattering_energy.y += fresnel * specular_sample + (1.0 - fresnel); // Dielectric
		}
	}
	
	float2 wave_single_scattering_energy = WaveActiveSum(single_scattering_energy);
	if (WaveIsFirstLane()) {
		gs_single_scattering_energy[thread_index / WaveGetLaneCount()] = wave_single_scattering_energy;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		float2 group_single_scattering_energy = wave_single_scattering_energy;
		
		u32 wave_count = thread_group_size / WaveGetLaneCount();
		for (u32 i = 1; i < wave_count; i += 1) {
			group_single_scattering_energy += gs_single_scattering_energy[i];
		}
		
		ggx_single_scattering_energy_lut[group_id] = group_single_scattering_energy / sample_count;
	}
}
#endif // defined(ENERGY_COMPENSATION_LUT)
