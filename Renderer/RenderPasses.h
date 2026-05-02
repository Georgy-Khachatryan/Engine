#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicString.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "MaterialAsset.h"
#include "MeshAsset.h"
#include "Renderer.h"
#include "RendererEntities.h"

NOTES()
enum struct VirtualResourceID : u32 {
	None = 0,
	
	// GPU components:
	MeshEntityAliveMask,
	MeshEntityGpuTransform,
	MeshEntityPrevGpuTransform,
	GpuMeshEntityData,
	GpuMeshAssetData,
	MeshAssetAliveMask,
	MaterialAssetTextureData,
	
	// Streaming buffers:
	MeshAssetBuffer,
	MeshletRtasBuffer,
	MeshletBlasBuffer,
	StreamingScratchBuffer,
	
	// Core resources:
	CurrentBackBuffer,
	TransientUploadBuffer,
	TransientReadbackBuffer,
	
	// Common scene resources:
	DepthStencil,
	SceneRadiance,
	MotionVectors,
	SceneRadianceResult,
	SceneConstants,
	CullingHZB,
	CullingHzbBuildState,
	TlasMeshInstances,
	SceneTLAS,
	
	// Mesh rendering:
	VisibleMeshlets,
	MeshEntityCullingCommands,
	MeshletGroupCullingCommands,
	MeshletCullingCommands,
	MeshletIndirectArguments,
	InstanceMeshletCounts,
	MeshletRtasIndirectArguments,
	
	// Streaming feedback:
	MeshletStreamingFeedback,
	MeshStreamingFeedback,
	TextureStreamingFeedback,
	
	// Atmosphere resources:
	TransmittanceLut,
	MultipleScatteringLut,
	SkyPanoramaLut,
	
	// Reference Path Tracer:
	ReferencePathTracerRadiance,
	GgxSingleScatteringEnergyLUT,
	
	// Debug geometry:
	DebugGeometryDepthStencil,
	DebugMeshBuffer,
	
	// Opaque handles from external SDKs:
	XessHandle,
	DlssHandle,
	
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


NOTES(Meta::ShaderName{ "EntitySystemUpdate.hlsl"_sl })
enum struct EntitySystemUpdateShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(EntitySystemUpdateShaders);

NOTES(Meta::HlslFile{ "EntitySystemUpdateData.hlsl"_sl })
enum struct GpuComponentUpdateFlags : u32 {
	None        = 0,
	CopyHistory = 1u << 0,
	InitHistory = 1u << 1,
};

struct GpuComponentUploadBuffer {
	u32 count  = 0;
	u32 stride = 0;
	
	u8*  data_cpu_address    = nullptr;
	u32* indices_cpu_address = nullptr;
	
	GpuAddress data_gpu_address;
	GpuAddress indices_gpu_address;
	GpuAddress dst_data_gpu_address;
	GpuAddress dst_prev_data_gpu_address;
};

template<typename T>
inline GpuComponentUploadBuffer AllocateGpuComponentUploadBuffer(RecordContext* record_context, u64 count, ECS::GpuComponent<T> buffer, ECS::GpuComponent<T> prev_buffer = {}) {
	extern GpuComponentUploadBuffer AllocateGpuComponentUploadBuffer(RecordContext* record_context, u32 stride, u32 count, GpuAddress dst_data_gpu_address, GpuAddress dst_prev_data_gpu_address);
	return AllocateGpuComponentUploadBuffer(record_context, sizeof(T), (u32)count, GpuAddress(buffer.resource_id, buffer.offset), GpuAddress(prev_buffer.resource_id, prev_buffer.offset));
};

template<typename T>
inline void AppendGpuTransferCommand(GpuComponentUploadBuffer& view, u64 dst_index, const T& element, GpuComponentUpdateFlags flags = GpuComponentUpdateFlags::None) {
	u32 src_index = view.count++;
	((T*)view.data_cpu_address)[src_index] = element;
	view.indices_cpu_address[src_index]    = (u32)dst_index | ((u32)flags << 30u);
};

NOTES(Meta::RenderPass{})
struct EntitySystemUpdateRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	AssetEntitySystem* asset_system  = nullptr;
	ArrayView<GpuComponentUploadBuffer> upload_buffers;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::ByteBuffer   src_data;
		HLSL::ByteBuffer   dst_indices;
		HLSL::RWByteBuffer dst_data;
		HLSL::RWByteBuffer dst_prev_data;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 count  = 0;
			u32 stride = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
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
		HLSL::Texture2D<float3>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
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
		HLSL::Texture2D<float3>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float3>   multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
		HLSL::RWTexture2D<float4> sky_panorama_lut        = VirtualResourceID::SkyPanoramaLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct AtmosphereCompositeRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3> transmittance_lut = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float3> sky_panorama_lut = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float>  depth_stencil    = VirtualResourceID::DepthStencil;
		HLSL::RWTexture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
		HLSL::RWTexture2D<float2> motion_vectors = VirtualResourceID::MotionVectors;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "UpdateMeshletPageTable.hlsl"_sl })
enum struct UpdateMeshletPageTableShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(UpdateMeshletPageTableShaders);

NOTES(Meta::HlslFile{ "UpdateMeshletPageTableData.hlsl"_sl })
enum struct MeshletPageUpdateCommandType : u16 {
	PageIn  = 0, // Page is streamed in, set it in the page table.
	RtasIn  = 1, // Page RTAS is ready.
	PageOut = 2, // Page is being removed, remove it from the page table.
};

NOTES(Meta::HlslFile{ "UpdateMeshletPageTableData.hlsl"_sl })
struct MeshletPageUpdateCommand {
	MeshletPageUpdateCommandType type = MeshletPageUpdateCommandType::PageIn;
	u16 readback_index     = 0;
	u16 asset_page_index   = 0;
	u16 runtime_page_index = 0;
};

NOTES(Meta::HlslFile{ "UpdateMeshletPageTableData.hlsl"_sl })
struct MeshletPageTableUpdateCommand {
	u32 mesh_asset_index    = 0;
	u16 page_command_offset = 0;
	u16 page_command_count  = 0;
};

NOTES(Meta::RenderPass{})
struct UpdateMeshletPageTableRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletStreamingSystem* meshlet_streaming_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuMeshAssetData> mesh_asset_data = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<MeshletPageTableUpdateCommand> page_table_commands;
		HLSL::RegularBuffer<MeshletPageUpdateCommand>      page_commands;
		HLSL::RWRegularBuffer<MeshletPageHeader> page_header_readback;
		HLSL::RWByteBuffer mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "MeshletRTAS.hlsl"_sl })
enum struct MeshletRtasShaders : u32 {
	MeshletRtasDecodeVertexBuffer     = 1u << 0,
	MeshletRtasBuildIndirectArguments = 1u << 1,
	MeshletRtasWriteOffsets           = 1u << 2,
	MeshletRtasUpdateOffsets          = 1u << 3,
	MeshletBlasBuildIndirectArguments = 1u << 4,
	MeshletBlasWriteAddresses         = 1u << 5,
	BuildMeshEntityInstances          = 1u << 6,
};
SHADER_DEFINITION_GENERATED_CODE(MeshletRtasShaders);

NOTES(Meta::HlslFile{ "MeshletRtasData.hlsl"_sl })
struct MeshletRtasBuildIndirectArgumentsInputs {
	u32 runtime_page_index        = 0;
	u32 indirect_arguments_offset = 0;
	u32 vertex_buffer_offsets     = 0;
};

NOTES(Meta::HlslFile{ "MeshletRtasData.hlsl"_sl })
struct MeshletRtasDecodeVertexBufferInputs {
	u32 runtime_page_index    = 0;
	u32 mesh_asset_index      = 0;
	u32 vertex_buffer_offsets = 0;
};

NOTES(Meta::HlslFile{ "MeshletRtasData.hlsl"_sl })
struct MeshletRtasWriteOffsetsInputs {
	u32 runtime_page_index   = 0;
	u32 meshlet_descs_offset = 0;
};

NOTES(Meta::HlslFile{ "MeshletRtasData.hlsl"_sl })
struct MeshletRtasUpdateOffsetsInputs {
	u32 runtime_page_index = 0;
	u32 page_address_shift = 0;
};

NOTES(Meta::HlslFile{ "MeshletRtasData.hlsl"_sl })
enum struct MeshletRtasIndirectArgumentsLayout {
	VertexBufferAllocator,
	CompactionMoveCount,
	TlasMeshInstanceCount,
	BlasMeshletCount,
	CandidateBlasCount,
	CommittedBlasCount,
	
	Count
};

NOTES(Meta::RenderPass{})
struct MeshletRtasDecodeVertexBufferRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletStreamingSystem* meshlet_streaming_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32>                                 packed_group_indices;
		HLSL::RegularBuffer<MeshletRtasDecodeVertexBufferInputs> decode_vertex_buffer_inputs;
		HLSL::RWRegularBuffer<u32> rtas_indirect_arguments = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RegularBuffer<GpuMeshAssetData> mesh_asset_data = VirtualResourceID::GpuMeshAssetData;
		HLSL::ByteBuffer   mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		HLSL::RWByteBuffer scratch_buffer    = VirtualResourceID::StreamingScratchBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 vertex_buffer_scratch_offset = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletRtasBuildRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletStreamingSystem* meshlet_streaming_system = nullptr;
	u64 mesh_asset_buffer_address = 0;
	u64 scratch_buffer_address    = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<MeshletRtasBuildIndirectArgumentsInputs> meshlet_rtas_inputs;
		HLSL::ByteBuffer mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		HLSL::RWByteBuffer scratch_buffer = VirtualResourceID::StreamingScratchBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u64 mesh_asset_buffer_address = 0;
			u64 scratch_buffer_address    = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletRtasWriteOffsetsRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletStreamingSystem* meshlet_streaming_system = nullptr;
	u64 meshlet_rtas_buffer_address = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<MeshletRtasWriteOffsetsInputs>  write_offsets_inputs;
		HLSL::ByteBuffer   scratch_buffer    = VirtualResourceID::StreamingScratchBuffer;
		HLSL::RWByteBuffer mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		HLSL::RWByteBuffer page_size_readback;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u64 meshlet_rtas_buffer_address = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletRtasUpdateOffsetsRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletStreamingSystem* meshlet_streaming_system = nullptr;
	u64 meshlet_rtas_buffer_address = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<MeshletRtasUpdateOffsetsInputs> update_offsets_inputs;
		HLSL::RWRegularBuffer<u32> rtas_indirect_arguments = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RWByteBuffer scratch_buffer    = VirtualResourceID::StreamingScratchBuffer;
		HLSL::RWByteBuffer mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u64 meshlet_rtas_buffer_address = 0;
			u32 new_addresses_offset = 0;
			u32 old_addresses_offset = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletBlasBuildIndirectArgumentsRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	u64 scratch_buffer_address = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u32> rtas_indirect_arguments = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RWRegularBuffer<u32> instance_meshlet_counts = VirtualResourceID::InstanceMeshletCounts;
		HLSL::RWByteBuffer         scratch_buffer          = VirtualResourceID::StreamingScratchBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u64 scratch_buffer_address = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletBlasWriteAddressesRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	u64 meshlet_rtas_buffer_address = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<uint4> indirect_arguments      = VirtualResourceID::MeshletIndirectArguments;
		HLSL::ByteBuffer           mesh_asset_buffer       = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<uint2> visible_meshlets        = VirtualResourceID::VisibleMeshlets;
		HLSL::RWRegularBuffer<u32> instance_meshlet_counts = VirtualResourceID::InstanceMeshletCounts;
		HLSL::RWByteBuffer         scratch_buffer          = VirtualResourceID::StreamingScratchBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u64 meshlet_rtas_buffer_address = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct BuildTlasRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u32>        rtas_indirect_arguments = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RegularBuffer<u32>          mesh_alive_mask         = VirtualResourceID::MeshEntityAliveMask;
		HLSL::ByteBuffer                  mesh_asset_buffer       = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<GpuTransform> mesh_transforms         = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<u32>          instance_meshlet_counts = VirtualResourceID::InstanceMeshletCounts;
		HLSL::RWByteBuffer                tlas_mesh_instances     = VirtualResourceID::TlasMeshInstances;
		HLSL::RWByteBuffer                scratch_buffer          = VirtualResourceID::StreamingScratchBuffer;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "MeshletCulling.hlsl"_sl })
enum struct MeshletCullingShaders : u32 {
	ClearBuffers              = 1u << 0,
	AllocateStreamingFeedback = 1u << 1,
	MeshEntityCulling         = 1u << 2,
	MeshletGroupCulling       = 1u << 3,
	MeshletCulling            = 1u << 4,
	ReadbackStatistics        = 1u << 5,
	MainPass                  = 1u << 6,
	DisocclusionPass          = 1u << 7,
	RaytracingPass            = 1u << 8,
};
SHADER_DEFINITION_GENERATED_CODE(MeshletCullingShaders);

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletConstants {
	compile_const u32 meshlet_culling_thread_group_size = 256u;
	
	compile_const u32 visible_meshlet_buffer_size = 1024 * 1024;
	
	compile_const u32 disocclusion_bin_count = 1;
	compile_const u32 disocclusion_bin_index = u32_max;
	
	compile_const u32 mesh_entity_culling_command_bin_size = 16 * 1024;
	compile_const u32 mesh_entity_culling_command_count    = mesh_entity_culling_command_bin_size * disocclusion_bin_count;
	
	compile_const u32 meshlet_group_culling_command_bin_count = 8;
	compile_const u32 meshlet_group_culling_command_bin_size  = 16 * 1024;
	compile_const u32 meshlet_group_culling_command_count     = meshlet_group_culling_command_bin_size * (meshlet_group_culling_command_bin_count + disocclusion_bin_count);
	
	compile_const u32 meshlet_culling_command_bin_count  = 6;
	compile_const u32 meshlet_culling_command_bin_size   = 16 * 1024;
	compile_const u32 meshlet_culling_command_count      = meshlet_culling_command_bin_size * (meshlet_culling_command_bin_count + disocclusion_bin_count);
	
	compile_const u32 max_meshlet_blas_count  = 512;
	compile_const u32 max_total_blas_meshlets = 64 * 1024;
	compile_const u32 max_meshlets_per_blas   = 16 * 1024;
	
	compile_const u32 blas_build_indirect_arguments_offset = 0;
	compile_const u32 blas_build_result_blas_descs_offset  = blas_build_indirect_arguments_offset + MeshletConstants::max_meshlet_blas_count * 16u;
	compile_const u32 blas_build_meshlet_addresses_offset  = blas_build_result_blas_descs_offset  + MeshletConstants::max_meshlet_blas_count * 16u;
	compile_const u32 blas_build_scratch_offset            = blas_build_meshlet_addresses_offset  + MeshletConstants::max_total_blas_meshlets * 8u;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
enum struct MeshletCullingIndirectArgumentsLayout : u32 {
	DispatchMesh,
	DisocclusionDispatchMesh,
	RaytracingBuildBLAS,
	
	MeshletGroupCullingCommands,
	MeshletGroupCullingEnd = MeshletGroupCullingCommands + MeshletConstants::meshlet_group_culling_command_bin_count - 1,
	
	MeshletCullingCommands,
	MeshletCullingCommandsEnd = MeshletCullingCommands + MeshletConstants::meshlet_culling_command_bin_count - 1,
	
	DisocclusionMeshletGroupCullingCommands,
	DisocclusionMeshletGroupCullingEnd = DisocclusionMeshletGroupCullingCommands + MeshletConstants::meshlet_group_culling_command_bin_count - 1,
	
	DisocclusionMeshletCullingCommands,
	DisocclusionMeshletCullingCommandsEnd = DisocclusionMeshletCullingCommands + MeshletConstants::meshlet_culling_command_bin_count - 1,
	
	RetestMeshEntityCullingCommands,
	RetestMeshletGroupCullingCommands,
	RetestMeshletCullingCommands,
	
	RaytracingMeshletGroupCullingCommands,
	RaytracingMeshletGroupCullingEnd = RaytracingMeshletGroupCullingCommands + MeshletConstants::meshlet_group_culling_command_bin_count - 1,
	
	RaytracingMeshletCullingCommands,
	RaytracingMeshletCullingCommandsEnd = RaytracingMeshletCullingCommands + MeshletConstants::meshlet_culling_command_bin_count - 1,
	
	Count
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
enum struct MeshletCullingPass : u32 {
	Main         = 0,
	Disocclusion = 1,
	Raytracing   = 2,
	
	Count
};

NOTES(Meta::RenderPass{})
struct MeshletClearBuffersRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u32> rtas_indirect_arguments    = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RWRegularBuffer<uint4> indirect_arguments       = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWRegularBuffer<u32> meshlet_streaming_feedback = VirtualResourceID::MeshletStreamingFeedback;
		HLSL::RWRegularBuffer<u32> mesh_streaming_feedback    = VirtualResourceID::MeshStreamingFeedback;
		HLSL::RWRegularBuffer<u32> texture_streaming_feedback = VirtualResourceID::TextureStreamingFeedback;
		HLSL::RWRegularBuffer<u32> culling_hzb_build_state    = VirtualResourceID::CullingHzbBuildState;
		HLSL::RWRegularBuffer<u32> instance_meshlet_counts    = VirtualResourceID::InstanceMeshletCounts;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 meshlet_streaming_feedback_size = 0;
			u32 mesh_streaming_feedback_size    = 0;
			u32 texture_streaming_feedback_size = 0;
			u32 mesh_instance_capacity          = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshletAllocateStreamingFeedbackRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	AssetEntitySystem* asset_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32> mesh_asset_alive_mask = VirtualResourceID::MeshAssetAliveMask;
		HLSL::RWRegularBuffer<GpuMeshAssetData> mesh_asset_data = VirtualResourceID::GpuMeshAssetData;
		HLSL::RWRegularBuffer<u32> meshlet_streaming_feedback = VirtualResourceID::MeshletStreamingFeedback;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MeshEntityCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	MeshletCullingPass pass = MeshletCullingPass::Main;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32>               mesh_alive_mask   = VirtualResourceID::MeshEntityAliveMask;
		HLSL::RegularBuffer<GpuTransform> prev_mesh_transforms   = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms   = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data   = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data  = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		
		HLSL::Texture2D<float> culling_hzb = VirtualResourceID::CullingHZB;
		
		HLSL::RWRegularBuffer<u32>   mesh_streaming_feedback        = VirtualResourceID::MeshStreamingFeedback;
		HLSL::RWRegularBuffer<u32>   mesh_entity_culling_commands   = VirtualResourceID::MeshEntityCullingCommands;
		HLSL::RWRegularBuffer<uint2> meshlet_group_culling_commands = VirtualResourceID::MeshletGroupCullingCommands;
		HLSL::RWRegularBuffer<uint4> indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static FixedCountArray<PipelineID, (u32)MeshletCullingPass::Count> pipeline_ids;
};

NOTES(Meta::RenderPass{})
struct MeshletGroupCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletCullingPass pass = MeshletCullingPass::Main;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuTransform> prev_mesh_transforms   = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms   = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data   = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data  = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		
		HLSL::Texture2D<float> culling_hzb = VirtualResourceID::CullingHZB;
		
		HLSL::RWRegularBuffer<uint2> meshlet_group_culling_commands = VirtualResourceID::MeshletGroupCullingCommands;
		HLSL::RWRegularBuffer<uint2> meshlet_culling_commands = VirtualResourceID::MeshletCullingCommands;
		HLSL::RWRegularBuffer<uint4> indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWRegularBuffer<u32> meshlet_streaming_feedback = VirtualResourceID::MeshletStreamingFeedback;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 bin_index = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static FixedCountArray<PipelineID, (u32)MeshletCullingPass::Count> pipeline_ids;
};

NOTES(Meta::RenderPass{})
struct MeshletCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletCullingPass pass = MeshletCullingPass::Main;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuTransform> prev_mesh_transforms   = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms   = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data   = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data  = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data = VirtualResourceID::MaterialAssetTextureData;
		
		HLSL::Texture2D<float> culling_hzb = VirtualResourceID::CullingHZB;
		
		HLSL::RWRegularBuffer<u32> texture_streaming_feedback = VirtualResourceID::TextureStreamingFeedback;
		HLSL::RWRegularBuffer<uint2> meshlet_culling_commands = VirtualResourceID::MeshletCullingCommands;
		HLSL::RWRegularBuffer<uint2> visible_meshlets         = VirtualResourceID::VisibleMeshlets;
		HLSL::RWRegularBuffer<uint4> indirect_arguments       = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWRegularBuffer<u32>   instance_meshlet_counts  = VirtualResourceID::InstanceMeshletCounts;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 bin_index = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static FixedCountArray<PipelineID, (u32)MeshletCullingPass::Count> pipeline_ids;
};

NOTES(Meta::RenderPass{})
struct CopyMeshletCullingStatisticsRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuReadbackQueue* readback_queue = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<uint4> indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWByteBuffer meshlet_culling_statistics;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct CopyStreamingFeedbackRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuReadbackQueue* meshlet_streaming_feedback_queue = nullptr;
	GpuReadbackQueue* mesh_streaming_feedback_queue    = nullptr;
	GpuReadbackQueue* texture_streaming_feedback_queue = nullptr;
};


NOTES(Meta::ShaderName{ "BuildHZB.hlsl"_sl })
enum struct BuildHzbShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(BuildHzbShaders);

NOTES(Meta::RenderPass{})
struct BuildHzbRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	compile_const u32 culling_hzb_build_state_size = (64u * 64u + 1) * sizeof(u32);
	compile_const u32 culling_hzb_max_mip_count = 12;
	static TextureSize ComputeCullingHzbSize(uint2 render_target_size);
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float> depth_stencil = VirtualResourceID::DepthStencil;
		HLSL::RWRegularBuffer<u32> culling_hzb_build_state = VirtualResourceID::CullingHzbBuildState;
		FixedCountArray<HLSL::RWTexture2D<float>, culling_hzb_max_mip_count> culling_hzb;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 last_thread_group_index = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<SceneConstants> scene;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "RaytracingDebug.hlsl"_sl })
enum struct RaytracingDebugShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(RaytracingDebugShaders);

NOTES(Meta::RenderPass{})
struct RaytracingDebugRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float> depth_stencil = VirtualResourceID::DepthStencil;
		HLSL::TopLevelRTAS     scene_tlas    = VirtualResourceID::SceneTLAS;
		HLSL::RWTexture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<SceneConstants> scene;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "ReferencePathTracer.hlsl"_sl })
enum struct ReferencePathTracerShaders : u32 {
	None                  = 0u,
	ReferencePathTracer   = 1u << 0,
	EnergyCompensationLUT = 1u << 1,
};
SHADER_DEFINITION_GENERATED_CODE(ReferencePathTracerShaders);

NOTES(Meta::RenderPass{})
struct ReferencePathTracerRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuAddress atmosphere;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float2>                     ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
		HLSL::Texture2D<float3>                     sky_panorama_lut      = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float3>                     transmittance_lut     = VirtualResourceID::TransmittanceLut;
		HLSL::RegularBuffer<GpuTransform>           mesh_transforms       = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>       mesh_asset_data       = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData>      mesh_entity_data      = VirtualResourceID::GpuMeshEntityData;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data = VirtualResourceID::MaterialAssetTextureData;
		HLSL::ByteBuffer                            mesh_asset_buffer     = VirtualResourceID::MeshAssetBuffer;
		HLSL::TopLevelRTAS                          scene_tlas            = VirtualResourceID::SceneTLAS;
		HLSL::RWTexture2D<float4>                   path_tracer_radiance  = VirtualResourceID::ReferencePathTracerRadiance;
		HLSL::RWTexture2D<float4>                   scene_radiance        = VirtualResourceID::SceneRadiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::ConstantBuffer<AtmosphereParameters> atmosphere;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct EnergyCompensationLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float2> ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
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
	
	MeshletCullingPass pass = MeshletCullingPass::Main;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuTransform> prev_mesh_transforms  = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms  = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data  = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data = VirtualResourceID::GpuMeshEntityData;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data = VirtualResourceID::MaterialAssetTextureData;
		HLSL::ByteBuffer           mesh_asset_buffer  = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<uint2> visible_meshlets   = VirtualResourceID::VisibleMeshlets;
		HLSL::RegularBuffer<uint4> indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			MeshletCullingPass pass = MeshletCullingPass::Main;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::RenderPass{})
struct DlssRenderPass {
	RENDER_PASS_GENERATED_CODE();
	float2 jitter_offset_pixels;
};

NOTES(Meta::RenderPass{})
struct XessRenderPass {
	RENDER_PASS_GENERATED_CODE();
	float2 jitter_offset_pixels;
};


NOTES(Meta::ShaderName{ "DebugGeometry.hlsl"_sl })
enum struct DebugGeometryShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(DebugGeometryShaders);

NOTES(Meta::RenderPass{ CommandQueueType::Graphics })
struct DebugGeometryRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	ArrayView<DebugMeshInstanceArray> debug_mesh_instance_arrays;
	DebugGeometryBuffer* debug_geometry_buffer = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<float4>            vertices;
		HLSL::RegularBuffer<DebugMeshInstance> instances;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			DebugMeshInstanceType instance_type = DebugMeshInstanceType::Sphere;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
	
	static DebugGeometryBuffer CreateDebugGeometryBuffer(StackAllocator* alloc, GraphicsContext* graphics_context, AsyncTransferQueue* async_transfer_queue);
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

