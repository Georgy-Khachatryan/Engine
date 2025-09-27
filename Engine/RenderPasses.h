#pragma once
#include "Basic/Basic.h"

#define NOTES(...)
#define RENDER_PASS_GENERATED_CODE() static struct RootSignature root_signature;

// Single line comment.
/*
	Multi line comment.
*/

namespace Meta {
	struct RenderGraphSystem {};
	struct RenderPass {};
};

namespace HLSL {
	template<typename T> struct Texture2D   {};
	template<typename T> struct RWTexture2D {};
	
	template<typename T> struct RegularBuffer   {};
	template<typename T> struct RWRegularBuffer {};
	
	template<typename T> struct DescriptorTable { u32 offset = 0; };
	template<typename T> struct ConstantBuffer  { u32 offset = 0; };
};


struct float2 { float values[2]; };
struct float3 { float values[3]; };
struct float4 { float values[4]; };

struct AtmosphereParameters {
	float bottom_radius; // Radius of the planet (center to ground)
	float top_radius;    // Maximum considered atmosphere height (center to atmosphere top)
	
	float  rayleigh_density_exp_scale; // Rayleigh scattering exponential distribution scale in the atmosphere.
	float3 rayleigh_scattering;        // Rayleigh scattering coefficients.
	
	float  mie_density_exp_scale; // Mie scattering exponential distribution scale in the atmosphere
	float3 mie_scattering; // Mie scattering coefficients
	float3 mie_absorption; // Mie absorption coefficients
	float  mie_phase_g;    // Mie phase function excentricity
	
	// Ozone layer (no scattering, absorption only).
	float  ozone_density_layer_height;
	float2 ozone_density_scale;
	float2 ozone_density_offset;
	float3 ozone_absorption;
};


NOTES(Meta::RenderPass{})
struct TransittanceLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors {
		HLSL::RWTexture2D<float4> transmittance_lut;
	};
	
	struct RootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};
};

NOTES(Meta::RenderPass{})
struct MultipleScatteringLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors {
		HLSL::Texture2D<float4>   transmittance_lut;
		HLSL::RWTexture2D<float4> multiple_scattering_lut;
	};
	
	struct RootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};
};

NOTES(Meta::RenderPass{})
struct SkyPanoramaLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors {
		HLSL::Texture2D<float4>   transmittance_lut;
		HLSL::Texture2D<float4>   multiple_scattering_lut;
		HLSL::RWTexture2D<float4> sky_panorama_lut;
	};
	
	struct RootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};
};

