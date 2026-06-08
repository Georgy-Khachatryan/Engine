#include "Basic.hlsl"

#if defined(REFERENCE_PATH_TRACER)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "MaterialSampling.hlsl"
#include "MeshletVertexDecoding.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

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
			float3 conductor_f0 = properties.albedo;
			float  alpha        = Pow2(roughness);
			float  alpha_square = Pow2(alpha);
			float3 diffuse_albedo = properties.albedo;
			
			LightSample light_sample = SampleLightUniform(ray_desc.Origin, hash);
			
			if (light_sample.light_entity_index != u32_max) {
				float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
				float3x3 tangent_to_world = transpose(world_to_tangent);
				
				LightShadingInfo shading_info = ComputeLightShadingInfo(ray_desc.Origin, light_sample.light_entity_index);
				shading_info.light_irradiance *= light_sample.inv_pdf;
				
				float3 wi = mul(world_to_tangent, shading_info.light_direction);
				float3 wo = mul(world_to_tangent, -ray_desc.Direction);
				float3 wh = normalize(wo + wi);
				float abs_cos_theta_o = abs(wo.z);
				float i_dot_h = saturate(dot(wi, wh));
				
				float3 shadowed_light_irradiance = (throughput * shading_info.light_irradiance) * saturate(wi.z);
				if (any(shadowed_light_irradiance > 0.0)) {
					shadowed_light_irradiance *= TraceShadowRay(ray_desc.Origin, shading_info.light_direction, shading_info.shadow_ray_length);
				}
				
				float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
				
				if (metalness != 0.0 && (wi.z * abs_cos_theta_o) > 0.0) {
					float3 specular_fresnel = FresnelConductor(conductor_f0, i_dot_h);
					float3 energy_compensation = ComputeConductorBrdfEnergyCompensation(single_scattering_energy, specular_fresnel) * metalness;
					
					float3 specular_brdf = specular_fresnel * SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * abs_cos_theta_o);
					
					radiance += shadowed_light_irradiance * (energy_compensation * specular_brdf);
				}
				
				if (metalness != 1.0 && (wi.z * abs_cos_theta_o) > 0.0) {
					float specular_fresnel = FresnelDielectric(dielectric_f0, i_dot_h);
					float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy) * (1.0 - metalness);
					
					float specular_brdf = specular_fresnel * SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * abs_cos_theta_o);
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
				float abs_cos_theta_o = abs(wo.z);
				float i_dot_h = saturate(dot(wi, wh));
				
				if ((wi.z * abs_cos_theta_o) > 0.0) {
					float2 mis_thresholds = ComputeRandomUnorm16x2(hash);
					
					float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
					
					if (mis_thresholds.x < metalness) {
						float3 specular_fresnel = FresnelConductor(conductor_f0, i_dot_h);
						float3 energy_compensation = ComputeConductorBrdfEnergyCompensation(single_scattering_energy, specular_fresnel);
						
						ray_desc.Direction = mul(tangent_to_world, wi);
						
						throughput *= specular_fresnel * energy_compensation * (SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) / SmithVisibilityG1(abs_cos_theta_o, alpha_square));
					} else {
						float specular_fresnel = FresnelDielectric(dielectric_f0, i_dot_h);
						float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy);
						
						float mis_specular_weight = specular_fresnel;
						float mis_diffuse_weight  = dot(diffuse_albedo, rec709_luminance_coefficients) * (1.0 - mis_specular_weight);
						float normalized_mis_specular_weight = mis_specular_weight / (mis_specular_weight + mis_diffuse_weight);
						float normalized_mis_diffuse_weight  = 1.0 - normalized_mis_specular_weight;
						
						if (mis_thresholds.y < normalized_mis_specular_weight) {
							ray_desc.Direction = normalize(mul(tangent_to_world, wi));
							
							throughput *= specular_fresnel * (energy_compensation * (SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) / SmithVisibilityG1(abs_cos_theta_o, alpha_square)) / normalized_mis_specular_weight);
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
		scene_radiance[thread_id] = float4(new_accumulated_radiance * scene.exposure_estimate, 1.0);
	}
}
#endif // defined(REFERENCE_PATH_TRACER)


#if defined(ENERGY_COMPENSATION_LUT)
#include "BrdfSampling.hlsl"

compile_const u32 thread_group_size   = 1024;
compile_const u32 sample_grid_size_xy = 128;
compile_const u32 sample_count        = sample_grid_size_xy * sample_grid_size_xy;
compile_const u32 samples_per_thread  = sample_count / thread_group_size;
compile_const u32 min_wave_size       = 16;

groupshared float4 gs_single_scattering_energy[thread_group_size / min_wave_size];

[ThreadGroupSize(thread_group_size, 1, 1)][WaveSize(min_wave_size, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	float2 group_uv = group_id * (1.0 / (energy_compensation_lut_size - 1));
	
	float roughness    = group_uv.y;
	float alpha        = Pow2(roughness);
	float alpha_square = Pow2(alpha);
	float cos_theta    = saturate(group_uv.x + energy_compensation_lut_cos_theta_bias);
	
	float3 wo = float3(sqrt(1.0 - Pow2(cos_theta)), 0.0, cos_theta);
	
	float4 single_scattering_energy = 0.0;
	for (u32 i = 0; i < samples_per_thread; i += 1) {
		u32 sample_index = thread_index * samples_per_thread + i;
		
		// Sample UV have exclusive upper bound, i.e. the range is [0, 1).
		float2 sample_uv = uint2(sample_index % sample_grid_size_xy, sample_index / sample_grid_size_xy) * (1.0 / sample_grid_size_xy);
		
		float3 wh = SampleTrowbridgeReitzVNDF(sample_uv, wo, alpha);
		float3 wi = reflect(-wo, wh);
		
		if ((wi.z * wo.z) > 0.0) {
			float specular_sample = SmithVisibilityG(wo.z, wi.z, alpha_square) / SmithVisibilityG1(wo.z, alpha_square);
			single_scattering_energy.x += specular_sample; // Conductor
			
			float fresnel = FresnelDielectric(dielectric_f0, dot(wo, wh));
			single_scattering_energy.y += fresnel * specular_sample + (1.0 - fresnel); // Dielectric
			
			float fresnel_zero_reflectance = FresnelSchlick(0.0, dot(wo, wh));
			single_scattering_energy.z += specular_sample * (1.0 - fresnel_zero_reflectance);
			single_scattering_energy.w += specular_sample * fresnel_zero_reflectance;
		}
	}
	
	float4 wave_single_scattering_energy = WaveActiveSum(single_scattering_energy);
	if (WaveIsFirstLane()) {
		gs_single_scattering_energy[thread_index / WaveGetLaneCount()] = wave_single_scattering_energy;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		float4 group_single_scattering_energy = wave_single_scattering_energy;
		
		u32 wave_count = thread_group_size / WaveGetLaneCount();
		for (u32 i = 1; i < wave_count; i += 1) {
			group_single_scattering_energy += gs_single_scattering_energy[i];
		}
		
		ggx_single_scattering_energy_lut[group_id] = group_single_scattering_energy.xy / sample_count;
		ggx_preintegrated_brdf_lut[group_id]       = group_single_scattering_energy.zw / sample_count;
	}
}
#endif // defined(ENERGY_COMPENSATION_LUT)
