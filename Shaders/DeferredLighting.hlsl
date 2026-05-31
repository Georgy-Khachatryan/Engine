#include "Basic.hlsl"
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"

compile_const u32 thread_group_size = 16;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) return;
	
	uint hash = WyHash32(thread_id.x | (thread_id.y << 16), scene.frame_index);
	
	float3 view_space_position = TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_position, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, normalize(view_space_position));
	
	float4 albedo_metalness = gb_albedo_metalness[thread_id];
	float4 normal_roughness = gb_normal_roughness[thread_id];
	
	float  metalness    = albedo_metalness.w;
	float  roughness    = normal_roughness.z;
	float3 conductor_f0 = albedo_metalness.xyz;
	float  alpha        = Pow2(roughness);
	float  alpha_square = Pow2(alpha);
	float3 diffuse_albedo = albedo_metalness.xyz;
	
	float3 world_space_normal = DecodeHemiOctahedralMap01(normal_roughness.xy) * float3(1.0, 1.0, normal_roughness.w * 2.0 - 1.0);
	ray_desc.Origin += world_space_normal * (1.0 / 1024.0);
	
	float blue_noise = blue_noise_1d[uint3(thread_id % 128, scene.frame_index % 32)];
	LightSample light_sample = SampleLightWithBlueNoise(ray_desc.Origin, blue_noise);
	
	float3 radiance   = 0.0;
	float3 throughput = 1.0;
	
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
	
	scene_radiance[thread_id] = float4(radiance * scene.exposure_estimate, 1);
}
