#ifndef BRDFSAMPLING_HLSL
#define BRDFSAMPLING_HLSL

compile_const u32   energy_compensation_lut_size           = 32u;
compile_const float energy_compensation_lut_cos_theta_bias = (1.0 / 1024.0);
compile_const float dielectric_f0 = 0.04;

enum struct VolumeInteractionType : u32 {
	None       = 0,
	Absorption = 1,
	Scattering = 2,
};


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
	// return SmithVisibilityG1(cos_theta_o, alpha_square) * SmithVisibilityG1(cos_theta_i, alpha_square); // Uncorrelated.
}


float PhaseFunctionHG(float cos_theta, float g) {
	float denom = 1.0 + Pow2(g) + 2.0 * g * cos_theta;
	return (1.0 - Pow2(g)) * ONE_OVER_FOUR_PI / (denom * sqrt(denom));
}

float3 SamplePhaseFunctionHG(float3 wo, float g, float2 u) {
	float cos_theta = 0.0;
	if (abs(g) < (1.0 / 128.0)) {
		cos_theta = 1.0 - 2.0 * u.x;
	} else {
		float numerator = (1.0 - Pow2(g)) / (1.0 + g - 2.0 * g * u.x);
		cos_theta = -(1.0 + Pow2(g) - Pow2(numerator)) / (2.0 * g);
	}
	
	float sin_theta = sqrt(max(1.0 - Pow2(cos_theta), 0.0));
	float phi = u.y * TAU;
	
	float3 wi = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
	
	float3x3 world_to_tangent = BuildOrthonormalBasis(wo);
	float3x3 tangent_to_world = transpose(world_to_tangent);
	
	return mul(tangent_to_world, wi);
}

float SampleExponentialDistribution(float u, float extinction_coefficients) {
	return -log(1.0 - u) / extinction_coefficients;
}

bool RussianRoulette(inout float throughput, float threshold, float u) {
	bool terminate_path = false;
	
	if (any(throughput < threshold)) {
		float q = max(1.0 - throughput, 0.0);
		terminate_path = u < q;
		throughput = terminate_path ? 0.0 : throughput / (1.0 - q);
	}
	
	return terminate_path;
}


template<typename T>
T SamplePreintegratedBrdfTable(Texture2D<T> preintegrated_lut, float cos_theta, float roughness) {
	float2 lut_parameters = saturate(float2(cos_theta + energy_compensation_lut_cos_theta_bias, roughness));
	float2 lut_uv = LutParametersToUv(lut_parameters, energy_compensation_lut_size);
	
	return preintegrated_lut.SampleLevel(sampler_linear_clamp, lut_uv, 0);
}

// Based on "Practical multiple scattering compensation for microfacet models" by Emmanuel Turquin.
float3 ComputeConductorBrdfEnergyCompensation(float3 single_scattering_energy, float3 specular_fresnel) {
	return (1.0 + specular_fresnel * (1.0 - single_scattering_energy.x) / single_scattering_energy.x);
}

float ComputeDielectricBrdfEnergyCompensation(float3 single_scattering_energy) {
	return (1.0 / single_scattering_energy.y);
}

float ComputeDielectricDiffuseBrdfEnergyCompensation(float3 single_scattering_energy) {
	return (single_scattering_energy.z / single_scattering_energy.y);
}

#endif // BRDFSAMPLING_HLSL
