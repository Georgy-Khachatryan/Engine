#include "Basic.hlsl"

#if defined(INDIRECT_DIFFUSE)
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"
#include "LightEvaluation.hlsl"
#include "GeometrySampling.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) {
		indirect_diffuse[thread_id] = 0;
		return;
	}
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), scene.frame_index);
	
	float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef);
	
	float4 normal_roughness   = gb_normal_roughness[thread_id];
	float3 world_space_normal = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	
	float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
	float3x3 tangent_to_world = transpose(world_to_tangent);
	
	float2 diffuse_blue_noise = blue_noise_2d[uint3((thread_id + scene.blue_noise_base_offset) % 128, scene.frame_index % 32)];
	
	RayDesc ray_desc;
	ray_desc.Origin = mul(scene.view_to_world, float4(view_space_position, 1.0));
	ray_desc.TMin   = 0.0;
	ray_desc.TMax   = 1024.0;
	ray_desc.Direction = mul(tangent_to_world, CosineWeightedHemisphereMapping(diffuse_blue_noise));
	
	ray_desc.Origin += world_space_normal * (1.0 / 1024.0);
	
	RayQuery<
		RAY_FLAG_CULL_NON_OPAQUE |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
		// RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
		RAY_FLAG_NONE
	> ray_query;
	
	ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
	
	while (ray_query.Proceed()) {
		
	}
	
	LightAccumulator light_accumulator;
	light_accumulator.radiance = 0.0;
	
	if (ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		MaterialProperties properties = SampleMaterialFromHitResult(
			NvRtGetCommittedClusterID(ray_query),
			ray_query.CommittedInstanceID(),
			ray_query.CommittedPrimitiveIndex(),
			ray_query.CommittedTriangleBarycentrics(),
			ray_query.CommittedTriangleFrontFace()
		);
		
		float3 world_space_normal = properties.normal;
		
		ray_desc.Origin += ray_desc.Direction * ray_query.CommittedRayT() + world_space_normal * (1.0 / 1024.0);
		
		float  metalness    = properties.metalness;
		float  roughness    = properties.roughness;
		float3 conductor_f0 = properties.albedo;
		float  alpha        = Pow2(roughness);
		float  alpha_square = Pow2(alpha);
		float3 diffuse_albedo = properties.albedo;
		
		LightSample light_sample = SampleLightWRS(ray_desc.Origin, world_space_normal, ComputeRandomUnorm16x2(hash).x);
		
		if (light_sample.light_entity_index != u32_max) {
			float3x3 world_to_tangent = BuildOrthonormalBasis(world_space_normal);
			float3x3 tangent_to_world = transpose(world_to_tangent);
			
			float3 wo = mul(world_to_tangent, -ray_desc.Direction);
			float abs_cos_theta_o = abs(wo.z);
			
			float2 single_scattering_energy = SampleGgxSingleScatteringEnergyLUT(ggx_single_scattering_energy_lut, abs_cos_theta_o, roughness);
			
			ShadowSampler shadow_sampler;
			shadow_sampler.penumbra_noise = ConcentricMapping(ComputeRandomUnorm16x2(hash));
			
			EvaluateBRDF(
				light_accumulator,
				shadow_sampler,
				ray_desc.Origin,
				world_to_tangent,
				wo,
				abs_cos_theta_o,
				metalness,
				roughness,
				alpha_square,
				conductor_f0,
				diffuse_albedo,
				/*throughput=*/1.0,
				single_scattering_energy,
				light_sample
			);
		}
	} else {
		light_accumulator.radiance = SampleSkyPanoramaLUT(atmosphere, sky_panorama_lut, transmittance_lut, scene.world_space_camera_position, ray_desc.Direction, false);
	}
	
	indirect_diffuse[thread_id] = EncodeR9G9B9E5(light_accumulator.radiance * scene.exposure_estimate);
}
#endif // defined(INDIRECT_DIFFUSE)
