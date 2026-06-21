#ifndef LIGHTEVALUATION_HLSL
#define LIGHTEVALUATION_HLSL
#include "Basic.hlsl"
#include "BrdfSampling.hlsl"
#include "LightSampling.hlsl"

struct LightAccumulator {
	float3 radiance;
	
	void AddSpecular(float3 new_radiance) { radiance += new_radiance; }
	void AddDiffuse(float3 new_radiance)  { radiance += new_radiance; }
};

struct SplitLightAccumulator {
	float3 specular_radiance;
	float3 diffuse_radiance;
	
	void AddSpecular(float3 new_radiance) { specular_radiance += new_radiance; }
	void AddDiffuse(float3 new_radiance)  { diffuse_radiance  += new_radiance; }
};

template<typename LightAccumulatorT, RAY_FLAG shadow_ray_flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH>
float EvaluateBRDF(
	inout LightAccumulatorT light_accumulator,
	float2 penumbra_noise,
	float3 shading_position,
	float3x3 world_to_tangent,
	float3 wo,
	float abs_cos_theta_o,
	float metalness,
	float roughness,
	float alpha_square,
	float3 conductor_f0,
	float3 diffuse_albedo,
	float3 throughput,
	float2 single_scattering_energy,
	LightSample light_sample
) {
	LightShadingInfo shading_info = ComputeLightShadingInfo(shading_position, light_sample.light_entity_index);
	shading_info.light_irradiance *= light_sample.inv_pdf;
	
	float3 wi = mul(world_to_tangent, shading_info.light_direction);
	float3 wh = normalize(wo + wi);
	float i_dot_h = saturate(dot(wi, wh));
	
	float3 shadowed_light_irradiance = (throughput * shading_info.light_irradiance) * saturate(wi.z);
	float penumbra_mask = 0.0;
	
	if (any(shadowed_light_irradiance > 0.0)) {
		float2 shadow_trace_result = TraceShadowRay<shadow_ray_flags>(shading_position, shading_info.light_direction, shading_info.shadow_ray_length, penumbra_noise);
		shadowed_light_irradiance *= shadow_trace_result.x;
		penumbra_mask = shadow_trace_result.y;
	}
	
	bool evaluate_brdf = any(shadowed_light_irradiance > 0.0) && (wi.z * abs_cos_theta_o) > 0.0;
	
	if (evaluate_brdf && metalness != 0.0) {
		float3 specular_fresnel = FresnelConductor(conductor_f0, i_dot_h);
		float3 energy_compensation = ComputeConductorBrdfEnergyCompensation(single_scattering_energy, specular_fresnel) * metalness;
		
		float3 specular_brdf = specular_fresnel * SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * abs_cos_theta_o);
		
		light_accumulator.AddSpecular(shadowed_light_irradiance * (energy_compensation * specular_brdf));
	}
	
	if (evaluate_brdf && metalness != 1.0) {
		float specular_fresnel = FresnelDielectric(dielectric_f0, i_dot_h);
		float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy) * (1.0 - metalness);
		
		float specular_brdf = specular_fresnel * SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) * TrowbridgeReitzNDF(wh.z, alpha_square) * rcp(4.0 * wi.z * abs_cos_theta_o);
		float3 diffuse_brdf = diffuse_albedo * ((1.0 - specular_fresnel) * rcp(PI));
		
		light_accumulator.AddSpecular(shadowed_light_irradiance * (energy_compensation * specular_brdf));
		light_accumulator.AddDiffuse(shadowed_light_irradiance * (energy_compensation * diffuse_brdf));
	}
	
	return penumbra_mask;
}

struct BrdfSampleResult {
	float3 wi;
	float3 throughput;
	bool is_valid;
};

BrdfSampleResult SampleBRDF(
	float3 wo,
	float abs_cos_theta_o,
	float metalness,
	float alpha,
	float alpha_square,
	float3 conductor_f0,
	float3 diffuse_albedo,
	float2 single_scattering_energy,
	inout uint hash
) {
	float3 wh = SampleTrowbridgeReitzVNDF(ComputeRandomUnorm16x2(hash), wo, alpha);
	float3 wi = reflect(-wo, wh);
	float i_dot_h = saturate(dot(wi, wh));
	
	BrdfSampleResult result;
	result.is_valid = (wi.z * abs_cos_theta_o) > 0.0;
	
	if (result.is_valid) {
		float2 mis_thresholds = ComputeRandomUnorm16x2(hash);
		
		if (mis_thresholds.x < metalness) {
			float3 specular_fresnel    = FresnelConductor(conductor_f0, i_dot_h);
			float3 energy_compensation = ComputeConductorBrdfEnergyCompensation(single_scattering_energy, specular_fresnel);
			
			result.wi         = wi;
			result.throughput = specular_fresnel * energy_compensation * (SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) / SmithVisibilityG1(abs_cos_theta_o, alpha_square));
		} else {
			float specular_fresnel    = FresnelDielectric(dielectric_f0, i_dot_h);
			float energy_compensation = ComputeDielectricBrdfEnergyCompensation(single_scattering_energy);
			
			float mis_specular_weight = specular_fresnel;
			float mis_diffuse_weight  = dot(diffuse_albedo, rec709_luminance_coefficients) * (1.0 - mis_specular_weight);
			float normalized_mis_specular_weight = mis_specular_weight / (mis_specular_weight + mis_diffuse_weight);
			float normalized_mis_diffuse_weight  = 1.0 - normalized_mis_specular_weight;
			
			if (mis_thresholds.y < normalized_mis_specular_weight) {
				result.wi         = wi;
				result.throughput = specular_fresnel * (energy_compensation * (SmithVisibilityG(abs_cos_theta_o, wi.z, alpha_square) / SmithVisibilityG1(abs_cos_theta_o, alpha_square)) / normalized_mis_specular_weight);
			} else {
				result.wi         = CosineWeightedHemisphereMapping(ComputeRandomUnorm16x2(hash));
				result.throughput = diffuse_albedo * (energy_compensation * (1.0 - specular_fresnel) / normalized_mis_diffuse_weight);
			}
		}
	}
	
	return result;
}

#endif // LIGHTEVALUATION_HLSL
