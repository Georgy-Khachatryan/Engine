#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct RecordContext;
struct PipelineLibrary;

#define RENDER_PASS_GENERATED_CODE()\
	struct RootSignature;\
	static RootSignature root_signature;\
	static void CreatePipelines(PipelineLibrary* lib);\
	void RecordPass(RecordContext* record_context)

#define SHADER_DEFINITION_GENERATED_CODE(name)\
	extern ShaderID name##ID;\
	ENUM_FLAGS_OPERATORS(name)


namespace Meta {
	NOTES() struct RenderGraphSystem {};
	NOTES() struct RenderPass {};
	NOTES() struct HlslFile { String filename; };
	NOTES() struct ShaderName { String filename; };
};


enum struct VirtualResourceID : u32;

namespace HLSL {
	enum struct ResourceDescriptorType : u16 {
		None            = 0,
		Texture2D       = 1,
		RWTexture2D     = 2,
		RegularBuffer   = 3,
		RWRegularBuffer = 4,
		ByteBuffer      = 5,
		RWByteBuffer    = 6,
	};
	
	struct ResourceDescriptor {
		using Type = ResourceDescriptorType;
		VirtualResourceID resource_id;
		
		union {
			struct {
				Type type = Type::None;
			} common = {};
			
			struct {
				Type type;
				u16 stride;
				
				u32 offset;
				u32 size;
			} buffer;
			
			struct {
				Type type;
				
				u8 mip_index;
				u8 mip_count;
				
				u16 array_index;
				u16 array_count;
				
				u32 padding_2;
			} texture;
		};
	};
	static_assert(sizeof(ResourceDescriptor) == 16, "Incorrect ResourceDescriptor size.");
	
	NOTES() template<typename T> struct Texture2D : ResourceDescriptor {
		Texture2D(VirtualResourceID resource = (VirtualResourceID)0, u32 mip_offset = 0, u32 mip_count = u32_max, u32 array_index = 0) { Bind(resource, mip_offset, mip_count, array_index); }
		
		void Bind(VirtualResourceID resource, u32 mip_offset = 0, u32 mip_count = u32_max, u32 array_index = 0) {
			resource_id = resource;
			texture = { Type::Texture2D, (u8)mip_offset, (u8)mip_count, (u16)array_index, 1, 0 };
		}
	};
	
	NOTES() template<typename T> struct RWTexture2D : ResourceDescriptor {
		RWTexture2D(VirtualResourceID resource = (VirtualResourceID)0, u32 mip_index = 0, u32 array_index = 0) { Bind(resource, mip_index, array_index); }
		
		void Bind(VirtualResourceID resource, u32 mip_index = 0, u32 array_index = 0) {
			resource_id = resource;
			texture = { Type::RWTexture2D, (u8)mip_index, 1, (u16)array_index, 1, 0 };
		}
	};
	
	NOTES() template<typename T> struct RegularBuffer : ResourceDescriptor {
		RegularBuffer(VirtualResourceID resource, u32 offset = 0, u32 size = u32_max) { Bind(resource, offset, size); }
		
		void Bind(VirtualResourceID resource, u32 offset = 0, u32 size = u32_max) {
			resource_id = resource;
			buffer = { Type::RegularBuffer, (u16)sizeof(T), offset, size };
		}
	};
	
	NOTES() template<typename T> struct RWRegularBuffer : ResourceDescriptor {
		RWRegularBuffer(VirtualResourceID resource, u32 offset = 0, u32 size = u32_max) { Bind(resource, offset, size); }
		
		void Bind(VirtualResourceID resource, u32 offset = 0, u32 size = u32_max) {
			resource_id = resource;
			buffer = { Type::RWRegularBuffer, (u16)sizeof(T), offset, size };
		}
	};
	
	NOTES() template<typename T> struct DescriptorTable { u32 offset = 0; u32 descriptor_count = 0; };
	NOTES() template<typename T> struct ConstantBuffer  { u32 offset = 0; };
	
	NOTES() struct BaseRootSignature   { u32 root_signature_index = 0; };
	NOTES() struct BaseDescriptorTable { u32 descriptor_heap_offset = 0; u32 descriptor_count = 0; };
};


NOTES() struct float2 { float x; float y; };
NOTES() struct float3 { float x; float y; float z; };
NOTES() struct float4 { float x; float y; float z; float w; };

NOTES(Meta::HlslFile{ "AtmosphereData.hlsl"_sl })
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


enum struct VirtualResourceID : u32 {
	TransmittanceLut,
	MultipleScatteringLut,
	SkyPanoramaLut,
	
	Count
};


NOTES(Meta::ShaderName{ "Atmosphere.hlsl"_sl })
enum struct AtmosphereShaders : u32 {
	TransmittanceLut      = 1u << 0,
	MultipleScatteringLut = 1u << 1,
	SkyPanoramaLut        = 1u << 2,
};
SHADER_DEFINITION_GENERATED_CODE(AtmosphereShaders);

NOTES(Meta::RenderPass{}, Meta::RenderGraphSystem{})
struct TransmittanceLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float4> transmittance_lut = VirtualResourceID::TransmittanceLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 sample_count  = 0;
			u32 group_count_x = 0;
		};
		
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MultipleScatteringLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float4>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::RWTexture2D<float4> multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};

	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct SkyPanoramaLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float4>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float4>   multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
		HLSL::RWTexture2D<float4> sky_panorama_lut        = VirtualResourceID::SkyPanoramaLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};

	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "DrawTriangle.hlsl"_sl })
enum struct DrawTriangleShaders : u32 {
	None      = 0,
	RedColor  = 1u << 0,
	BlueColor = 1u << 1,
};
SHADER_DEFINITION_GENERATED_CODE(DrawTriangleShaders);

NOTES(Meta::RenderPass{})
struct DrawTriangleRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct RootSignature : HLSL::BaseRootSignature {
	};
};
