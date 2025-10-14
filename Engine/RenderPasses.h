#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMath.h"
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


NOTES()
enum struct RenderPassType : u32 {
	Graphics = 0,
	Compute  = 1,
	
	Count
};

namespace Meta {
	NOTES() struct RenderGraphSystem {};
	NOTES() struct RenderPass { RenderPassType pass_type = RenderPassType::Compute; };
	NOTES() struct HlslFile { String filename; };
	NOTES() struct ShaderName { String filename; };
};


enum struct VirtualResourceID : u32;

namespace HLSL {
	enum struct ResourceDescriptorType : u16 {
		None = 0,
		
		AnyTexture  = 1u << 0,
		AnyBuffer   = 1u << 1,
		AnySRV      = 1u << 2,
		AnyUAV      = 1u << 3,
		
		Texture2D       = (0u << 4) | AnyTexture | AnySRV,
		RWTexture2D     = (1u << 4) | AnyTexture | AnyUAV,
		RegularBuffer   = (2u << 4) | AnyBuffer  | AnySRV,
		RWRegularBuffer = (3u << 4) | AnyBuffer  | AnyUAV,
		ByteBuffer      = (4u << 4) | AnyBuffer  | AnySRV,
		RWByteBuffer    = (5u << 4) | AnyBuffer  | AnyUAV,
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
		Texture2D(VirtualResourceID resource = (VirtualResourceID)0) { Bind(resource); }
		
		void Bind(VirtualResourceID resource, u32 mip_offset = 0, u32 mip_count = u32_max) {
			resource_id = resource;
			texture = { Type::Texture2D, (u8)mip_offset, (u8)mip_count, 0, 1, 0 };
		}
	};
	
	NOTES() template<typename T> struct RWTexture2D : ResourceDescriptor {
		RWTexture2D(VirtualResourceID resource = (VirtualResourceID)0) { Bind(resource); }
		
		void Bind(VirtualResourceID resource, u32 mip_index = 0) {
			resource_id = resource;
			texture = { Type::RWTexture2D, (u8)mip_index, 1, 0, 1, 0 };
		}
	};
	
	NOTES() template<typename T> struct RegularBuffer : ResourceDescriptor {
		RegularBuffer(VirtualResourceID resource = (VirtualResourceID)0) { Bind(resource); }
		
		void Bind(GpuAddress gpu_address, u32 size = u32_max) {
			resource_id = gpu_address.resource_id;
			buffer = { Type::RegularBuffer, (u16)sizeof(T), gpu_address.offset, size };
		}
	};
	
	NOTES() template<typename T> struct RWRegularBuffer : ResourceDescriptor {
		RWRegularBuffer(VirtualResourceID resource = (VirtualResourceID)0) { Bind(resource); }
		
		void Bind(GpuAddress gpu_address, u32 size = u32_max) {
			resource_id = gpu_address.resource_id;
			buffer = { Type::RWRegularBuffer, (u16)sizeof(T), gpu_address.offset, size };
		}
	};
	
	NOTES() template<typename T> struct DescriptorTable { u32 offset = 0; u32 descriptor_count = 0; };
	NOTES() template<typename T> struct ConstantBuffer  { u32 offset = 0; };
	NOTES() template<typename T> struct PushConstantBuffer { u32 offset = 0; };
	
	NOTES() struct BaseRootSignature   { u32 root_signature_index = 0; u32 root_parameter_count = 0; RenderPassType pass_type = RenderPassType::Graphics; };
	NOTES() struct BaseDescriptorTable { u32 descriptor_heap_offset = 0; u32 descriptor_count = 0; };
};


NOTES(Meta::HlslFile{ "AtmosphereData.hlsl"_sl })
struct AtmosphereParameters {
	compile_const u32   thread_group_size            = 16;
	compile_const uint2 transmittance_lut_size       = uint2(256, 64);
	compile_const uint2 multiple_scattering_lut_size = uint2(32, 32);
	compile_const uint2 sky_panorama_lut_size        = uint2(192, 128);
	
	float bottom_radius = 6360.f; // Radius of the planet (center to ground), km.
	float top_radius    = 6460.f; // Maximum considered atmosphere height (center to atmosphere top), km.
	uint2 padding_0 = 0;
	
	// Rayleigh scattering exponential distribution scale in the atmosphere.
	float  rayleigh_density_exp_scale = -1.f / 8.f;
	float3 rayleigh_scattering        = float3(0.005802f, 0.013558f, 0.033100f); // 1/km
	
	// Mie scattering exponential distribution scale in the atmosphere
	float  mie_density_exp_scale = -1.f / 1.2f;
	float3 mie_scattering = float3(0.003996f, 0.003996f, 0.003996f); // 1/km
	float3 mie_absorption = float3(0.000444f, 0.000444f, 0.000444f); // 1/km
	float  mie_phase_g    = 0.8f;
	
	// Ozone layer (no scattering, absorption only). Two layers, the first one below layer_height, the second one is above.
	float2 ozone_density_scale  = float2(+1.f / 15.f, -1.f / 15.f);
	float2 ozone_density_offset = float2(-2.f /  3.f,  8.f /  3.f);
	float3 ozone_absorption     = float3(0.000650f, 0.001881f, 0.000085f);
	float  ozone_density_layer_height = 25.f; // km
	
	float3 world_space_sun_direction = float3(1.f, 0.f, 0.f);
	float sun_irradiance = 30.f; // W/m^2
	
	float sun_disk_cos_outer_angle = 0.9999572f; // cos(0.53dg)
	float sun_disk_cos_inner_angle = 0.9999649f; // cos(0.48dg)
	float sun_disk_radiance        = 30.f; // W/(m^2*sr)
	u32 padding_1 = 0;
};

NOTES(Meta::HlslFile{ "SceneData.hlsl"_sl })
struct SceneConstants {
	float2 render_target_size;
	float2 inv_render_target_size;
	float4 view_to_clip_coef;
	float4 clip_to_view_coef;
	float3x4 view_to_world;
};

enum struct VirtualResourceID : u32 {
	None = 0,
	
	// Core resources:
	CurrentBackBuffer,
	TransientUploadBuffer,
	
	// Common scene resources:
	SceneRadiance,
	
	// Atmosphere resources:
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
	AtmosphereComposite   = 1u << 3,
};
SHADER_DEFINITION_GENERATED_CODE(AtmosphereShaders);

NOTES(Meta::RenderPass{}, Meta::RenderGraphSystem{})
struct TransmittanceLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float4> transmittance_lut = VirtualResourceID::TransmittanceLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 sample_count  = 0;
			u32 group_count_x = 0;
		};
		
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MultipleScatteringLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float4>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::RWTexture2D<float4> multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct SkyPanoramaLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float4>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float4>   multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
		HLSL::RWTexture2D<float4> sky_panorama_lut        = VirtualResourceID::SkyPanoramaLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct AtmosphereCompositeRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	GpuAddress scene_constants;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float4> transmittance_lut = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float4> sky_panorama_lut = VirtualResourceID::SkyPanoramaLut;
		HLSL::RWTexture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "ImGui.hlsl"_sl })
enum struct ImGuiShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(ImGuiShaders);


compile_const String imgui_data_filename = "ImGuiData.hlsl"_sl;

NOTES(Meta::HlslFile{ imgui_data_filename })
struct ImGuiVertex {
	float2 position;
	float2 texcoord;
	u32    color;
};

NOTES(Meta::HlslFile{ imgui_data_filename })
struct ImGuiPushConstants { float4 view_to_clip_coef; };

NOTES(Meta::HlslFile{ imgui_data_filename })
struct ImGuiTextureIdPushConstants { u32 index = 0; };

NOTES(Meta::RenderPass{ RenderPassType::Graphics })
struct ImGuiRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<ImGuiVertex> vertices;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::PushConstantBuffer<ImGuiTextureIdPushConstants> texture_id;
		HLSL::PushConstantBuffer<ImGuiPushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID sdr_pipeline_id;
	inline static PipelineID hdr_pipeline_id;
};

