#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMath.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "Components.h"

NOTES()
enum struct VirtualResourceID : u32 {
	None = 0,
	
	MeshEntityGpuTransform,
	
	// Core resources:
	CurrentBackBuffer,
	TransientUploadBuffer,
	
	// Common scene resources:
	DepthStencil,
	SceneRadiance,
	
	// Mesh rendering:
	VisibleMeshlets,
	MeshletIndirectArguments,
	
	// Atmosphere resources:
	TransmittanceLut,
	MultipleScatteringLut,
	SkyPanoramaLut,
	
	Count
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
	float3x4 world_to_view;
	
	float world_to_pixel_scale;
	float3 world_space_camera_position;
};


NOTES(Meta::ShaderName{ "EntitySystemUpdate.hlsl"_sl })
enum struct EntitySystemUpdateShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(EntitySystemUpdateShaders);

struct EntitySystem;
struct GpuComponentUploadBuffer {
	u32 count  = 0;
	u32 stride = 0;
	
	u8*  data_cpu_address    = nullptr;
	u32* indices_cpu_address = nullptr;
	
	GpuAddress data_gpu_address;
	GpuAddress indices_gpu_address;
	GpuAddress dst_data_gpu_address;
};

NOTES(Meta::HlslFile{ "EntitySystemUpdateData.hlsl"_sl })
struct EntitySystemUpdatePushConstants {
	u32 count  = 0;
	u32 stride = 0;
};

NOTES(Meta::RenderPass{})
struct EntitySystemUpdateRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	EntitySystem* entity_system = nullptr;
	ArrayView<GpuComponentUploadBuffer> upload_buffers;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::ByteBuffer   src_data;
		HLSL::ByteBuffer   dst_indices;
		HLSL::RWByteBuffer dst_data;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::PushConstantBuffer<EntitySystemUpdatePushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "Atmosphere.hlsl"_sl })
enum struct AtmosphereShaders : u32 {
	TransmittanceLut      = 1u << 0,
	MultipleScatteringLut = 1u << 1,
	SkyPanoramaLut        = 1u << 2,
	AtmosphereComposite   = 1u << 3,
};
SHADER_DEFINITION_GENERATED_CODE(AtmosphereShaders);

NOTES(Meta::RenderPass{})
struct TransmittanceLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float4> transmittance_lut = VirtualResourceID::TransmittanceLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
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



NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct BasicVertex {
	float3 position;
	float3 normal;
	float2 texcoord;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletErrorMetric {
	float3 center = 0.f;
	float  radius = 0.f;
	float  error  = 0.f;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct BasicMeshlet {
	u32 index_buffer_offset  = 0;
	u32 vertex_buffer_offset = 0;
	u32 triangle_count = 0;
	u32 vertex_count   = 0;
	
	MeshletErrorMetric current_level_error_metric;
	MeshletErrorMetric coarser_level_error_metric;
};

NOTES(Meta::ShaderName{ "MeshletCulling.hlsl"_sl })
enum struct MeshletCullingShaders : u32 {
	ClearBuffers   = 1u << 0,
	MeshletCulling = 1u << 1,
};
SHADER_DEFINITION_GENERATED_CODE(MeshletCullingShaders);

NOTES(Meta::RenderPass{})
struct MeshletClearBuffersRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<uint4> indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress scene_constants;
	GpuAddress meshlet_buffer;
	u32 meshlet_count = 0;
	u32 instance_count = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuTransform> mesh_transforms = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RWRegularBuffer<uint2> visible_meshlets   = VirtualResourceID::VisibleMeshlets;
		HLSL::RWRegularBuffer<uint4> indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RegularBuffer<BasicMeshlet> meshlets;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "DrawTestMesh.hlsl"_sl })
enum struct DrawTestMeshShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(DrawTestMeshShaders);

NOTES(Meta::RenderPass{ CommandQueueType::Graphics })
struct BasicMeshRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress scene_constants;
	
	GpuAddress vertex_buffer;
	GpuAddress meshlet_buffer;
	GpuAddress index_buffer;
	
	u32 vertex_count  = 0;
	u32 meshlet_count = 0;
	u32 index_buffer_size = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuTransform> mesh_transforms = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<uint2> visible_meshlets = VirtualResourceID::VisibleMeshlets;
		HLSL::RegularBuffer<BasicVertex>  vertices;
		HLSL::RegularBuffer<BasicMeshlet> meshlets;
		HLSL::ByteBuffer              index_buffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
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

NOTES(Meta::RenderPass{ CommandQueueType::Graphics })
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

