#include "Basic.hlsl"
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"

compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;


#define USE_VISIBLE_LIGHT_HASH_MASK 1

#if USE_VISIBLE_LIGHT_HASH_MASK
compile_const u32 visible_light_tiles_per_thread_group = thread_group_area / LightCullingConstants::visible_light_tile_area;

groupshared uint4 gs_visible_light_hash_mask[visible_light_tiles_per_thread_group];
groupshared uint  gs_tile_is_first_lane[visible_light_tiles_per_thread_group];
#endif // USE_VISIBLE_LIGHT_HASH_MASK

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	
#if USE_VISIBLE_LIGHT_HASH_MASK
	uint2 hash_mask_size = scene.visible_light_hash_mask_size;
	
	float2 motion_uv_offset = motion_vectors[thread_id];
	float2 src_tile_blue_noise = ConcentricMapping(blue_noise_2d[uint3(thread_id % 128, scene.frame_index % 32)]);
	
	s32x2 src_tile_id = (s32x2)round((thread_uv + motion_uv_offset) * hash_mask_size + (src_tile_blue_noise - 0.5));
	uint2 dst_tile_id = (thread_id / LightCullingConstants::visible_light_tile_size);
	
	uint src_tile_index = (hash_mask_size.x * src_tile_id.y + src_tile_id.x) + (scene.frame_index & 0x1 ? hash_mask_size.x * hash_mask_size.y : 0);
	uint dst_tile_index = (hash_mask_size.x * dst_tile_id.y + dst_tile_id.x) + (scene.frame_index & 0x1 ? 0 : hash_mask_size.x * hash_mask_size.y);
	
	bool src_tile_valid = all(src_tile_id >= 0) && all(src_tile_id < hash_mask_size);
	
	// TODO: Experiment with randomly seeding light hashes.
	// In most cases this doesn't do anything, but in some
	// regions with a lot of overlapping lights and hash
	// collisions, random seeding reduces the noise a lot.
	uint src_tile_hash_seed = WyHash32(src_tile_index, scene.frame_index + 0u);
	uint dst_tile_hash_seed = WyHash32(dst_tile_index, scene.frame_index + 1u);
	
	if (thread_index < visible_light_tiles_per_thread_group) {
		gs_visible_light_hash_mask[thread_index] = 0;
		gs_tile_is_first_lane[thread_index] = 1;
	}
#endif // USE_VISIBLE_LIGHT_HASH_MASK
	
	
	float depth = depth_stencil[thread_id];
	if (depth == 0.0) {
		denoiser_radiance_source_s[thread_id] = 0;
		denoiser_radiance_source_d[thread_id] = 0;
		return;
	}
	
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
	
	float light_sampling_blue_noise = blue_noise_1d[uint3(thread_id % 128, scene.frame_index % 32)];
	
#if USE_VISIBLE_LIGHT_HASH_MASK
	LightSample light_sample = SampleLightWRS<true>(ray_desc.Origin, world_space_normal, light_sampling_blue_noise, src_tile_valid ? visible_light_hash_mask[src_tile_index] : 0u, src_tile_hash_seed);
#else // !USE_VISIBLE_LIGHT_HASH_MASK
	LightSample light_sample = SampleLightWRS(ray_desc.Origin, world_space_normal, light_sampling_blue_noise);
#endif // !USE_VISIBLE_LIGHT_HASH_MASK
	
	bool demodulate_radiance = true;
	float3 specular_radiance = 0.0;
	float3 diffuse_radiance  = 0.0;
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
			
			specular_radiance += shadowed_light_irradiance * (energy_compensation * specular_brdf);
		}
		
		if (metalness != 1.0 && (wi.z * abs_cos_theta_o) > 0.0) {
			float specular_fresnel = FresnelDielectric(dielectric_f0, i_dot_h);
			float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy) * (1.0 - metalness);
			
			float specular_brdf = specular_fresnel * SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * abs_cos_theta_o);
			float3 diffuse_brdf = (demodulate_radiance ? 1.0 : diffuse_albedo) * ((1.0 - specular_fresnel) * rcp(PI));
			
			specular_radiance += shadowed_light_irradiance * (energy_compensation * specular_brdf);
			diffuse_radiance  += shadowed_light_irradiance * (energy_compensation * diffuse_brdf);
		}
		
		if (demodulate_radiance) {
			float2 preintegrated_brdf = SampleGgxSingleScatteringEnergyLUT(ggx_preintegrated_brdf_lut, abs_cos_theta_o, roughness);
			float3 specular_demodulation = lerp(dielectric_f0, conductor_f0, metalness) * preintegrated_brdf.x + preintegrated_brdf.y;
			specular_radiance /= max(specular_demodulation, 1.0 / 64.0);
		}
	}
	
	denoiser_radiance_source_s[thread_id] = EncodeR9G9B9E5(specular_radiance * scene.exposure_estimate);
	denoiser_radiance_source_d[thread_id] = EncodeR9G9B9E5(diffuse_radiance  * scene.exposure_estimate);
	
	
#if USE_VISIBLE_LIGHT_HASH_MASK
	GroupMemoryBarrierWithGroupSync();
	
	uint tile_index_in_thread_group = (thread_index / LightCullingConstants::visible_light_tile_area);
	uint tile_is_first_lane = 0;
	
	if (any((diffuse_radiance + specular_radiance) > 0.0)) {
		u32 light_hash = (WyHash32(light_sample.light_entity_index, dst_tile_hash_seed) % 128u);
		GsBitArraySetBit(gs_visible_light_hash_mask[tile_index_in_thread_group], light_hash);
	}
	
	if (WaveIsFirstLane()) {
		InterlockedExchange(gs_tile_is_first_lane[tile_index_in_thread_group], 0u, tile_is_first_lane);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (tile_is_first_lane) {
		visible_light_hash_mask[dst_tile_index] = gs_visible_light_hash_mask[tile_index_in_thread_group];
	}
#endif // USE_VISIBLE_LIGHT_HASH_MASK
}
