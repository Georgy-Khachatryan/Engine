#pragma once
#include "Basic/Basic.h"

#define RENDER_PASS_GENERATED_CODE() static struct RootSignature root_signature;

// Single line comment.
/*
	Multi line comment.
*/

namespace Meta {
	struct RenderGraphSystem {};
	struct RenderPass {};
	enum struct RenderPassType : u32 { None = 0, Compute = 1, Graphics = 2, Count };
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

NOTES(Meta::HlslFile{ "AtmosphereData.hlsl" })
struct AtmosphereParameters {
	compile_const u32 transmittance_lut_width = 256;
	
	float bottom_radius = 6360.f; // Radius of the planet (center to ground).
	float top_radius    = 6460.f; // Maximum considered atmosphere height (center to atmosphere top).
	
	// Rayleigh scattering exponential distribution scale in the atmosphere.
	float  rayleigh_density_exp_scale = -1.f / 8.f;
	float3 rayleigh_scattering        = { 0.005802f, 0.013558f, 0.033100f };
	
	// Mie scattering exponential distribution scale in the atmosphere
	float  mie_density_exp_scale = -1.f / 1.2f;
	float3 mie_scattering = { 0.003996f, 0.003996f, 0.003996f };
	float3 mie_absorption = { 0.000444f, 0.000444f, 0.000444f };
	float  mie_phase_g    = 0.8f;
	
	// Ozone layer (no scattering, absorption only). Two layers, the first one below layer_height, the second one is above.
	float  ozone_density_layer_height = 25.f;
	float2 ozone_density_scale  = { +1.f / 15.f, -1.f / 15.f };
	float2 ozone_density_offset = { -2.f /  3.f,  8.f /  3.f };
	float3 ozone_absorption     = { 0.000650f, 0.001881f, 0.000085f };
};


NOTES(Meta::RenderPass{}, Meta::RenderGraphSystem{}, Meta::RenderPassType::Compute)
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

