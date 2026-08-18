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
	CloudVolumeAliveMask,
	GpuCloudVolumeEntityData,
	GpuLightEntityData,
	GpuMeshAssetData,
	GpuMeshEntityData,
	LightEntityAliveMask,
	MaterialAssetTextureData,
	MeshAssetAliveMask,
	MeshEntityAliveMask,
	MeshEntityGpuTransform,
	MeshEntityPrevGpuTransform,
	
	// Streaming buffers:
	MeshAssetBuffer,
	MeshletRtasBuffer,
	MeshletBlasBuffer,
	StreamingScratchBuffer,
	
	// Core resources:
	CurrentBackBuffer,
	ExternalOutput,
	TransientUploadBuffer,
	TransientReadbackBuffer,
	BlueNoise1D,
	BlueNoise2D,
	
	// Common scene resources:
	DepthStencil,
	DepthStencilHistory,
	SceneRadiance,
	VisibilityBuffer,
	GBufferAlbedoMetalness,
	GBufferNormalRoughness,
	MotionVectors,
	DepthMotionVectors,
	SceneRadianceResult,
	SceneConstants,
	CullingHZB,
	CullingHzbBuildState,
	TlasMeshInstances,
	SceneTLAS,
	LuminanceHistogram,
	Exposure,
	ExposureTexture,
	
	// Denoiser:
	DenoiserDisocclusionMask,
	DenoiserRadianceHistoryS0,
	DenoiserRadianceHistoryS1,
	DenoiserRadianceHistoryD0,
	DenoiserRadianceHistoryD1,
	DenoiserRadianceSourceS,
	DenoiserRadianceSourceD,
	DenoiserAccumulatedFrameCount0,
	DenoiserAccumulatedFrameCount1,
	DenoiserPenumbraMask0,
	DenoiserPenumbraMask1,
	
	// Mesh rendering:
	VisibleMeshlets,
	MeshEntityCullingCommands,
	MeshletGroupCullingCommands,
	MeshletCullingCommands,
	MeshletIndirectArguments,
	
	RtVisibleMeshlets,
	RtMeshletGroupCullingCommands,
	RtMeshletCullingCommands,
	
	InstanceMeshletCounts,
	MeshletRtasIndirectArguments,
	
	// Lighting:
	LightCullingIndirectArguments,
	LightCullingCommands,
	LightCullingGrid,
	VisibleLightTileList,
	
	VisibilityHashTableKeys,
	VisibilityHashTableValues,
	
	IndirectDiffuse,
	IndirectSpecular,
	IndirectDiffuseDirections,
	IndirectDiffuseTileCDF,
	TileCdfSolidAngle,
	
	CdfHashTableKeys,
	CdfHashTableValues,
	
	// Clouds:
	CloudCullingIndirectArguments,
	CloudCullingCommands,
	CloudCullingGrid,
	
	SdfCloudVolume,
	SdfCloudVolumeTransientMask,
	SdfCloudVolumeMask,
	
	CloudOpticalDepthVolume,
	CloudRadianceTransferVolume0,
	CloudRadianceTransferVolume1,
	CloudSampleCountVolume,
	
	CloudShadowMap0,
	CloudShadowMap1,
	TransientCloudShadowMap,
	
	// Radiance cache:
	RadianceHashTableKeys,
	RadianceHashTableValues,
	
	// Streaming feedback:
	MeshletStreamingFeedback,
	MeshStreamingFeedback,
	TextureStreamingFeedback,
	
	// Atmosphere resources:
	TransmittanceLut,
	MultipleScatteringLut,
	SkyPanoramaLut,
	AverageSkyIrradiance,
	
	// Reference Path Tracer:
	ReferencePathTracerRadiance,
	GgxSingleScatteringEnergyLUT,
	GgxPreintegratedBrdfLUT,
	
	// Debug geometry:
	DebugGeometryDepthStencil,
	DebugMeshBuffer,
	DebugMeshInstances,
	DebugGeometryIndirectArguments,
	
	// Opaque handles from external SDKs:
	XessHandle,
	DlssHandle,
	
	Count
};


NOTES(Meta::HlslFile{ "DebugGeometryData.hlsl"_sl })
struct DebugGeometryIndirectArguments {
	u32 index_count_per_instance = 0;
	u32 instance_count           = 0;
	u32 start_index_location     = 0;
	u32 base_vertex_location     = 0;
	u32 start_instance_location  = 0;
};

NOTES()
struct DebugDescriptorTable : HLSL::BaseDescriptorTable {
	HLSL::RWRegularBuffer<DebugMeshInstance>              debug_mesh_instances              = VirtualResourceID::DebugMeshInstances;
	HLSL::RWRegularBuffer<DebugGeometryIndirectArguments> debug_geometry_indirect_arguments = VirtualResourceID::DebugGeometryIndirectArguments;
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
	ArrayView<GpuComponentUploadBuffer> gpu_uploads;
	
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
	AverageSkyIrradiance  = 1u << 3,
	AtmosphereComposite   = 1u << 4,
};
SHADER_DEFINITION_GENERATED_CODE(AtmosphereShaders);

NOTES(Meta::RenderPass{})
struct TransmittanceLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float4> transmittance_lut = VirtualResourceID::TransmittanceLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct MultipleScatteringLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::RWTexture2D<float4> multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct SkyPanoramaLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>   transmittance_lut       = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float3>   multiple_scattering_lut = VirtualResourceID::MultipleScatteringLut;
		HLSL::RWTexture2D<float4> sky_panorama_lut        = VirtualResourceID::SkyPanoramaLut;
		HLSL::RWTexture2D<float4> average_sky_irradiance  = VirtualResourceID::AverageSkyIrradiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id_sky_panorama_lut;
	inline static PipelineID pipeline_id_average_sky_irradiance;
};

NOTES(Meta::RenderPass{})
struct AtmosphereCompositeRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3> transmittance_lut = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float3> sky_panorama_lut = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float>  depth_stencil    = VirtualResourceID::DepthStencil;
		HLSL::RWTexture2D<float4> scene_radiance = VirtualResourceID::SceneRadiance;
		HLSL::RWTexture2D<float2> motion_vectors = VirtualResourceID::MotionVectors;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
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
		
		HLSL::RWRegularBuffer<u32>            rtas_indirect_arguments = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RegularBuffer<GpuMeshAssetData> mesh_asset_data         = VirtualResourceID::GpuMeshAssetData;
		HLSL::ByteBuffer                      mesh_asset_buffer       = VirtualResourceID::MeshAssetBuffer;
		HLSL::RWByteBuffer                    scratch_buffer          = VirtualResourceID::StreamingScratchBuffer;
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
		
		HLSL::ByteBuffer   mesh_asset_buffer = VirtualResourceID::MeshAssetBuffer;
		HLSL::RWByteBuffer scratch_buffer    = VirtualResourceID::StreamingScratchBuffer;
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
		HLSL::RWByteBuffer         scratch_buffer          = VirtualResourceID::StreamingScratchBuffer;
		HLSL::RWByteBuffer         mesh_asset_buffer       = VirtualResourceID::MeshAssetBuffer;
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
		HLSL::RegularBuffer<uint2> visible_meshlets        = VirtualResourceID::RtVisibleMeshlets;
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
	compile_const u32 rt_meshlet_group_culling_command_count  = meshlet_group_culling_command_bin_size * meshlet_group_culling_command_bin_count;
	
	compile_const u32 meshlet_culling_command_bin_count  = 6;
	compile_const u32 meshlet_culling_command_bin_size   = 16 * 1024;
	compile_const u32 meshlet_culling_command_count      = meshlet_culling_command_bin_size * (meshlet_culling_command_bin_count + disocclusion_bin_count);
	compile_const u32 rt_meshlet_culling_command_count   = meshlet_culling_command_bin_size * meshlet_culling_command_bin_count;
	
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
	bool clear_streaming_feedback = false;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u32>   rtas_indirect_arguments          = VirtualResourceID::MeshletRtasIndirectArguments;
		HLSL::RWRegularBuffer<uint4> indirect_arguments               = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWRegularBuffer<u32>   meshlet_streaming_feedback       = VirtualResourceID::MeshletStreamingFeedback;
		HLSL::RWRegularBuffer<u32>   mesh_streaming_feedback          = VirtualResourceID::MeshStreamingFeedback;
		HLSL::RWRegularBuffer<u32>   texture_streaming_feedback       = VirtualResourceID::TextureStreamingFeedback;
		HLSL::RWRegularBuffer<u32>   culling_hzb_build_state          = VirtualResourceID::CullingHzbBuildState;
		HLSL::RWRegularBuffer<u32>   instance_meshlet_counts          = VirtualResourceID::InstanceMeshletCounts;
		HLSL::RWRegularBuffer<uint4> light_culling_indirect_arguments = VirtualResourceID::LightCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>   light_culling_grid               = VirtualResourceID::LightCullingGrid;
		HLSL::RWRegularBuffer<uint4> cloud_culling_indirect_arguments = VirtualResourceID::CloudCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>   cloud_culling_grid               = VirtualResourceID::CloudCullingGrid;
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
		HLSL::RegularBuffer<u32>                mesh_asset_alive_mask      = VirtualResourceID::MeshAssetAliveMask;
		HLSL::RWRegularBuffer<GpuMeshAssetData> mesh_asset_data            = VirtualResourceID::GpuMeshAssetData;
		HLSL::RWRegularBuffer<u32>              meshlet_streaming_feedback = VirtualResourceID::MeshletStreamingFeedback;
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
		HLSL::RegularBuffer<u32>               mesh_alive_mask      = VirtualResourceID::MeshEntityAliveMask;
		HLSL::RegularBuffer<GpuTransform>      prev_mesh_transforms = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms      = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data      = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data     = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer    = VirtualResourceID::MeshAssetBuffer;
		HLSL::Texture2D<float>                 culling_hzb          = VirtualResourceID::CullingHZB;
		
		HLSL::RWRegularBuffer<u32>   mesh_streaming_feedback        = VirtualResourceID::MeshStreamingFeedback;
		HLSL::RWRegularBuffer<u32>   mesh_entity_culling_commands   = VirtualResourceID::MeshEntityCullingCommands;
		HLSL::RWRegularBuffer<uint2> meshlet_group_culling_commands = VirtualResourceID::MeshletGroupCullingCommands;
		HLSL::RWRegularBuffer<uint4> indirect_arguments             = VirtualResourceID::MeshletIndirectArguments;
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
		HLSL::RegularBuffer<GpuTransform>      prev_mesh_transforms = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms      = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data      = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data     = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer    = VirtualResourceID::MeshAssetBuffer;
		HLSL::Texture2D<float>                 culling_hzb          = VirtualResourceID::CullingHZB;
		
		HLSL::RWRegularBuffer<uint2> meshlet_group_culling_commands = VirtualResourceID::MeshletGroupCullingCommands;
		HLSL::RWRegularBuffer<uint2> meshlet_culling_commands       = VirtualResourceID::MeshletCullingCommands;
		HLSL::RWRegularBuffer<uint4> indirect_arguments             = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWRegularBuffer<u32>   meshlet_streaming_feedback     = VirtualResourceID::MeshletStreamingFeedback;
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
		HLSL::RegularBuffer<GpuTransform>           prev_mesh_transforms  = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>           mesh_transforms       = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>       mesh_asset_data       = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData>      mesh_entity_data      = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                            mesh_asset_buffer     = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data = VirtualResourceID::MaterialAssetTextureData;
		HLSL::Texture2D<float>                      culling_hzb           = VirtualResourceID::CullingHZB;
		
		HLSL::RWRegularBuffer<u32>   texture_streaming_feedback = VirtualResourceID::TextureStreamingFeedback;
		HLSL::RWRegularBuffer<uint2> meshlet_culling_commands   = VirtualResourceID::MeshletCullingCommands;
		HLSL::RWRegularBuffer<uint2> visible_meshlets           = VirtualResourceID::VisibleMeshlets;
		HLSL::RWRegularBuffer<uint4> indirect_arguments         = VirtualResourceID::MeshletIndirectArguments;
		HLSL::RWRegularBuffer<u32>   instance_meshlet_counts    = VirtualResourceID::InstanceMeshletCounts;
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


NOTES(Meta::ShaderName{ "LightCulling.hlsl"_sl })
enum struct LightCullingShaders : u32 {
	LightEntityCulling = 1u << 0,
	LightCulling       = 1u << 1,
	LightList          = 1u << 2,
};
SHADER_DEFINITION_GENERATED_CODE(LightCullingShaders);

NOTES(Meta::HlslFile{ "LightData.hlsl"_sl })
struct LightCullingConstants {
	compile_const u32 thread_group_size = 256u;
	
	compile_const u32   grid_size_cells    = 16;
	compile_const u32   grid_cell_count    = grid_size_cells * grid_size_cells * grid_size_cells;
	compile_const u32   grid_cascade_count = SceneConstants::light_grid_cascade_count;
	compile_const float grid_cell_size     = 0.25f;
	compile_const float inv_grid_cell_size = 1.f / grid_cell_size;
	
	compile_const u32 max_elements_per_cell    = 64;
	compile_const u32 max_input_light_count    = max_elements_per_cell * 32;
	compile_const u32 max_lights_per_cell      = max_elements_per_cell - 1; // 1 Counter
	compile_const u32 max_elements_per_cascade = max_elements_per_cell * grid_cell_count;
	compile_const u32 grid_element_count       = max_elements_per_cascade * grid_cascade_count;
	
	compile_const u32 culling_command_bin_count = 13;
	compile_const u32 culling_command_bin_size  = max_input_light_count * grid_cascade_count;
	compile_const u32 culling_command_count     = culling_command_bin_size * culling_command_bin_count;
	
	static_assert((1u << (culling_command_bin_count - 1)) == (grid_size_cells * grid_size_cells * grid_size_cells));
};

NOTES(Meta::RenderPass{})
struct LightEntityCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32>                light_alive_mask       = VirtualResourceID::LightEntityAliveMask;
		HLSL::RegularBuffer<GpuLightEntityData> light_entity_data      = VirtualResourceID::GpuLightEntityData;
		HLSL::RWRegularBuffer<uint2>            light_culling_commands = VirtualResourceID::LightCullingCommands;
		HLSL::RWRegularBuffer<uint4>            indirect_arguments     = VirtualResourceID::LightCullingIndirectArguments;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct LightCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuLightEntityData> light_entity_data      = VirtualResourceID::GpuLightEntityData;
		HLSL::RegularBuffer<uint2>              light_culling_commands = VirtualResourceID::LightCullingCommands;
		HLSL::RWRegularBuffer<uint4>            indirect_arguments     = VirtualResourceID::LightCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>              light_culling_grid     = VirtualResourceID::LightCullingGrid;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 bin_index = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct LightListRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u32> light_culling_grid = VirtualResourceID::LightCullingGrid;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
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
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>                     ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
		HLSL::Texture2D<float3>                     sky_panorama_lut      = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float3>                     transmittance_lut     = VirtualResourceID::TransmittanceLut;
		HLSL::RegularBuffer<GpuLightEntityData>     light_entity_data     = VirtualResourceID::GpuLightEntityData;
		HLSL::RegularBuffer<GpuTransform>           mesh_transforms       = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>       mesh_asset_data       = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData>      mesh_entity_data      = VirtualResourceID::GpuMeshEntityData;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data = VirtualResourceID::MaterialAssetTextureData;
		HLSL::Texture3D<float>                      sdf_cloud_volume      = VirtualResourceID::SdfCloudVolume;
		HLSL::Texture3D<u64>                        sdf_cloud_volume_mask = VirtualResourceID::SdfCloudVolumeMask;
		HLSL::ByteBuffer                            mesh_asset_buffer     = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<u32>                    light_culling_grid    = VirtualResourceID::LightCullingGrid;
		HLSL::TopLevelRTAS                          scene_tlas            = VirtualResourceID::SceneTLAS;
		HLSL::RWTexture2D<float4>                   path_tracer_radiance  = VirtualResourceID::ReferencePathTracerRadiance;
		HLSL::RWTexture2D<float4>                   scene_radiance        = VirtualResourceID::SceneRadiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct EnergyCompensationLutRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float4> ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
		HLSL::RWTexture2D<float2> ggx_preintegrated_brdf_lut       = VirtualResourceID::GgxPreintegratedBrdfLUT;
		HLSL::RWTexture2D<float>  tile_cdf_solid_angle             = VirtualResourceID::TileCdfSolidAngle;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "VisibilityBufferLaydown.hlsl"_sl })
enum struct VisibilityBufferLaydownShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(VisibilityBufferLaydownShaders);

NOTES(Meta::RenderPass{ CommandQueueType::Graphics })
struct VisibilityBufferLaydownRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	MeshletCullingPass pass = MeshletCullingPass::Main;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuTransform>      mesh_transforms    = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>  mesh_asset_data    = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data   = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer  = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<uint2>             visible_meshlets   = VirtualResourceID::VisibleMeshlets;
		HLSL::RegularBuffer<uint4>             indirect_arguments = VirtualResourceID::MeshletIndirectArguments;
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


NOTES(Meta::ShaderName{ "VisibilityBufferResolve.hlsl"_sl })
enum struct VisibilityBufferResolveShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(VisibilityBufferResolveShaders);

NOTES(Meta::RenderPass{})
struct VisibilityBufferResolveRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<u32>                        visibility_buffer     = VirtualResourceID::VisibilityBuffer;
		HLSL::Texture2D<float>                      depth_stencil         = VirtualResourceID::DepthStencil;
		HLSL::RegularBuffer<GpuTransform>           prev_mesh_transforms  = VirtualResourceID::MeshEntityPrevGpuTransform;
		HLSL::RegularBuffer<GpuTransform>           mesh_transforms       = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>       mesh_asset_data       = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData>      mesh_entity_data      = VirtualResourceID::GpuMeshEntityData;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data = VirtualResourceID::MaterialAssetTextureData;
		HLSL::ByteBuffer                            mesh_asset_buffer     = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<uint2>                  visible_meshlets      = VirtualResourceID::VisibleMeshlets;
		HLSL::RWTexture2D<float2>                   motion_vectors        = VirtualResourceID::MotionVectors;
		HLSL::RWTexture2D<float>                    depth_motion_vectors  = VirtualResourceID::DepthMotionVectors;
		HLSL::RWTexture2D<float4>                   gb_albedo_metalness   = VirtualResourceID::GBufferAlbedoMetalness;
		HLSL::RWTexture2D<float4>                   gb_normal_roughness   = VirtualResourceID::GBufferNormalRoughness;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::HlslFile{ "LightData.hlsl"_sl })
struct LightingConstants {
	compile_const u32 cdf_mip_count                   = 4u;
	compile_const u32 cdf_tile_size                   = 1u << cdf_mip_count;
	compile_const u32 cdf_tile_area                   = cdf_tile_size * cdf_tile_size;
	compile_const float inv_cdf_tile_area             = 1.f / cdf_tile_area;
	compile_const u32 cdf_hash_table_atlas_size_tiles = 128u;
	compile_const u32 cdf_hash_table_atlas_size       = cdf_hash_table_atlas_size_tiles * cdf_tile_size;
	
	compile_const u32 visible_light_tile_size = 8;
	compile_const u32 visible_light_tile_area = visible_light_tile_size * visible_light_tile_size;
	
	compile_const u32 visibility_hash_table_size = 1024u * 1024u;
	compile_const u32 radiance_hash_table_size   = 1024u * 1024u;
	compile_const u32 cdf_hash_table_size        = cdf_hash_table_atlas_size_tiles * cdf_hash_table_atlas_size_tiles;
};

NOTES(Meta::ShaderName{ "DeferredLighting.hlsl"_sl })
enum struct DeferredLightingShaders : u32 {
	DeferredLighting          = 1u << 0,
	BuildVisibleLightTileList = 1u << 1,
	UpdateVisibilityHashTable = 1u << 2,
};
SHADER_DEFINITION_GENERATED_CODE(DeferredLightingShaders);

NOTES(Meta::RenderPass{})
struct DeferredLightingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>                 ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
		HLSL::Texture2D<float2>                 ggx_preintegrated_brdf_lut       = VirtualResourceID::GgxPreintegratedBrdfLUT;
		HLSL::Texture2D<u32>                    denoiser_disocclusion_mask       = VirtualResourceID::DenoiserDisocclusionMask;
		HLSL::Texture2DArray<float>             blue_noise_1d                    = VirtualResourceID::BlueNoise1D;
		HLSL::Texture2DArray<float2>            blue_noise_2d                    = VirtualResourceID::BlueNoise2D;
		HLSL::Texture2D<float3>                 transmittance_lut                = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2D<float>                  depth_stencil                    = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float4>                 gb_albedo_metalness              = VirtualResourceID::GBufferAlbedoMetalness;
		HLSL::Texture2D<float4>                 gb_normal_roughness              = VirtualResourceID::GBufferNormalRoughness;
		HLSL::Texture2D<float2>                 motion_vectors                   = VirtualResourceID::MotionVectors;
		HLSL::Texture2D<float3>                 indirect_diffuse                 = VirtualResourceID::IndirectDiffuse;
		HLSL::Texture2D<float3>                 indirect_specular                = VirtualResourceID::IndirectSpecular;
		HLSL::RegularBuffer<GpuLightEntityData> light_entity_data                = VirtualResourceID::GpuLightEntityData;
		HLSL::RegularBuffer<u32>                light_culling_grid               = VirtualResourceID::LightCullingGrid;
		HLSL::Texture2D<float3>                 cloud_shadow_map                 = VirtualResourceID::CloudShadowMap0;
		HLSL::TopLevelRTAS                      scene_tlas                       = VirtualResourceID::SceneTLAS;
		HLSL::Texture2D<float>                  denoiser_penumbra_mask_0         = VirtualResourceID::DenoiserPenumbraMask0;
		HLSL::RWTexture2D<float>                denoiser_penumbra_mask_1         = VirtualResourceID::DenoiserPenumbraMask1;
		HLSL::RWTexture2D<u32>                  denoiser_radiance_source_s       = VirtualResourceID::DenoiserRadianceSourceS;
		HLSL::RWTexture2D<u32>                  denoiser_radiance_source_d       = VirtualResourceID::DenoiserRadianceSourceD;
		HLSL::RWRegularBuffer<u32>              visible_light_tile_list          = VirtualResourceID::VisibleLightTileList;
		HLSL::RWRegularBuffer<u64>              visibility_hash_table_keys       = VirtualResourceID::VisibilityHashTableKeys;
		HLSL::RWRegularBuffer<u32>              visibility_hash_table_values     = VirtualResourceID::VisibilityHashTableValues;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct BuildVisibleLightTileListRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<u32>       denoiser_disocclusion_mask = VirtualResourceID::DenoiserDisocclusionMask;
		HLSL::RWRegularBuffer<u32> visible_light_tile_list    = VirtualResourceID::VisibleLightTileList;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct UpdateVisibilityHashTableRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u64> visibility_hash_table_keys   = VirtualResourceID::VisibilityHashTableKeys;
		HLSL::RWRegularBuffer<u32> visibility_hash_table_values = VirtualResourceID::VisibilityHashTableValues;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::ShaderName{ "IndirectLighting.hlsl"_sl })
enum struct IndirectLightingShaders : u32 {
	IndirectDiffuse         = 1u << 0,
	IndirectSpecular        = 1u << 1,
	UpdateRadianceHashTable = 1u << 2,
	UpdateCdfHashTable      = 1u << 3,
	IndirectDiffuseTileCDF  = 1u << 4,
};
SHADER_DEFINITION_GENERATED_CODE(IndirectLightingShaders);

NOTES(Meta::RenderPass{})
struct IndirectDiffuseRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>                     ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
		HLSL::Texture2D<float2>                     ggx_preintegrated_brdf_lut       = VirtualResourceID::GgxPreintegratedBrdfLUT;
		HLSL::Texture2D<float>                      tile_cdf_solid_angle             = VirtualResourceID::TileCdfSolidAngle;
		HLSL::Texture2D<float3>                     sky_panorama_lut                 = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float3>                     transmittance_lut                = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2DArray<float2>                blue_noise_2d                    = VirtualResourceID::BlueNoise2D;
		HLSL::Texture2D<float>                      depth_stencil                    = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float4>                     gb_normal_roughness              = VirtualResourceID::GBufferNormalRoughness;
		HLSL::Texture2D<float>                      indirect_diffuse_tile_cdf        = VirtualResourceID::IndirectDiffuseTileCDF;
		HLSL::RegularBuffer<GpuLightEntityData>     light_entity_data                = VirtualResourceID::GpuLightEntityData;
		HLSL::RegularBuffer<GpuTransform>           mesh_transforms                  = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>       mesh_asset_data                  = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData>      mesh_entity_data                 = VirtualResourceID::GpuMeshEntityData;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data            = VirtualResourceID::MaterialAssetTextureData;
		HLSL::ByteBuffer                            mesh_asset_buffer                = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<u32>                    light_culling_grid               = VirtualResourceID::LightCullingGrid;
		HLSL::Texture2D<float3>                     cloud_shadow_map                 = VirtualResourceID::CloudShadowMap0;
		HLSL::TopLevelRTAS                          scene_tlas                       = VirtualResourceID::SceneTLAS;
		HLSL::RWRegularBuffer<u64>                  radiance_hash_table_keys         = VirtualResourceID::RadianceHashTableKeys;
		HLSL::RWByteBuffer                          radiance_hash_table_values       = VirtualResourceID::RadianceHashTableValues;
		HLSL::RWRegularBuffer<u64>                  cdf_hash_table_keys              = VirtualResourceID::CdfHashTableKeys;
		HLSL::RWRegularBuffer<u32>                  cdf_hash_table_values            = VirtualResourceID::CdfHashTableValues;
		HLSL::RWTexture2D<u32>                      indirect_diffuse                 = VirtualResourceID::IndirectDiffuse;
		HLSL::RWTexture2D<u32>                      indirect_diffuse_directions      = VirtualResourceID::IndirectDiffuseDirections;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct IndirectSpecularRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>                     ggx_single_scattering_energy_lut = VirtualResourceID::GgxSingleScatteringEnergyLUT;
		HLSL::Texture2D<float2>                     ggx_preintegrated_brdf_lut       = VirtualResourceID::GgxPreintegratedBrdfLUT;
		HLSL::Texture2D<float3>                     sky_panorama_lut                 = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float3>                     transmittance_lut                = VirtualResourceID::TransmittanceLut;
		HLSL::Texture2DArray<float2>                blue_noise_2d                    = VirtualResourceID::BlueNoise2D;
		HLSL::Texture2D<float>                      depth_stencil                    = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float4>                     gb_albedo_metalness              = VirtualResourceID::GBufferAlbedoMetalness;
		HLSL::Texture2D<float4>                     gb_normal_roughness              = VirtualResourceID::GBufferNormalRoughness;
		HLSL::RegularBuffer<GpuLightEntityData>     light_entity_data                = VirtualResourceID::GpuLightEntityData;
		HLSL::RegularBuffer<GpuTransform>           mesh_transforms                  = VirtualResourceID::MeshEntityGpuTransform;
		HLSL::RegularBuffer<GpuMeshAssetData>       mesh_asset_data                  = VirtualResourceID::GpuMeshAssetData;
		HLSL::RegularBuffer<GpuMeshEntityData>      mesh_entity_data                 = VirtualResourceID::GpuMeshEntityData;
		HLSL::RegularBuffer<GpuMaterialTextureData> material_texture_data            = VirtualResourceID::MaterialAssetTextureData;
		HLSL::ByteBuffer                            mesh_asset_buffer                = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<u32>                    light_culling_grid               = VirtualResourceID::LightCullingGrid;
		HLSL::Texture2D<float3>                     cloud_shadow_map                 = VirtualResourceID::CloudShadowMap0;
		HLSL::TopLevelRTAS                          scene_tlas                       = VirtualResourceID::SceneTLAS;
		HLSL::RWRegularBuffer<u64>                  radiance_hash_table_keys         = VirtualResourceID::RadianceHashTableKeys;
		HLSL::RWByteBuffer                          radiance_hash_table_values       = VirtualResourceID::RadianceHashTableValues;
		HLSL::RWTexture2D<u32>                      indirect_specular                = VirtualResourceID::IndirectSpecular;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct UpdateRadianceHashTableRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u64> radiance_hash_table_keys   = VirtualResourceID::RadianceHashTableKeys;
		HLSL::RWByteBuffer         radiance_hash_table_values = VirtualResourceID::RadianceHashTableValues;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct IndirectDiffuseTileCdfRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32> cdf_hash_table_values       = VirtualResourceID::CdfHashTableValues;
		HLSL::RWTexture2D<u32>   indirect_diffuse_directions = VirtualResourceID::IndirectDiffuseDirections;
		FixedCountArray<HLSL::RWTexture2D<float>, LightingConstants::cdf_mip_count> indirect_diffuse_tile_cdf;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct UpdateCdfHashTableRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWRegularBuffer<u64> cdf_hash_table_keys   = VirtualResourceID::CdfHashTableKeys;
		HLSL::RWRegularBuffer<u32> cdf_hash_table_values = VirtualResourceID::CdfHashTableValues;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "LightingDenoiser.hlsl"_sl })
enum struct LightingDenoiserShaders : u32 {
	DisocclusionMask = 1u << 0,
	TemporalPass     = 1u << 1,
	SpatialPass      = 1u << 2,
};
SHADER_DEFINITION_GENERATED_CODE(LightingDenoiserShaders);

NOTES(Meta::RenderPass{})
struct DenoiserDisocclusionMaskRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float>  depth_stencil_history      = VirtualResourceID::DepthStencilHistory;
		HLSL::Texture2D<float>  depth_stencil              = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float4> gb_normal_roughness        = VirtualResourceID::GBufferNormalRoughness;
		HLSL::Texture2D<float2> motion_vectors             = VirtualResourceID::MotionVectors;
		HLSL::Texture2D<float>  depth_motion_vectors       = VirtualResourceID::DepthMotionVectors;
		HLSL::RWTexture2D<u32>  denoiser_disocclusion_mask = VirtualResourceID::DenoiserDisocclusionMask;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct LightingTemporalDenoiserRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float2>  ggx_preintegrated_brdf_lut         = VirtualResourceID::GgxPreintegratedBrdfLUT;
		HLSL::Texture2D<float>   depth_stencil_history              = VirtualResourceID::DepthStencilHistory;
		HLSL::Texture2D<float>   depth_stencil                      = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float4>  gb_albedo_metalness                = VirtualResourceID::GBufferAlbedoMetalness;
		HLSL::Texture2D<float4>  gb_normal_roughness                = VirtualResourceID::GBufferNormalRoughness;
		HLSL::Texture2D<float2>  motion_vectors                     = VirtualResourceID::MotionVectors;
		HLSL::Texture2D<u32>     denoiser_disocclusion_mask         = VirtualResourceID::DenoiserDisocclusionMask;
		HLSL::Texture2D<float>   denoiser_accumulated_frame_count_0 = VirtualResourceID::DenoiserAccumulatedFrameCount0;
		HLSL::Texture2D<float>   denoiser_penumbra_mask_0           = VirtualResourceID::DenoiserPenumbraMask0;
		HLSL::Texture2D<float3>  denoiser_radiance_source_s         = VirtualResourceID::DenoiserRadianceSourceS;
		HLSL::Texture2D<float3>  denoiser_radiance_source_d         = VirtualResourceID::DenoiserRadianceSourceD;
		HLSL::Texture2D<float3>  denoiser_radiance_history_s_0      = VirtualResourceID::DenoiserRadianceHistoryS0;
		HLSL::Texture2D<float3>  denoiser_radiance_history_d_0      = VirtualResourceID::DenoiserRadianceHistoryD0;
		HLSL::RWTexture2D<u32>   denoiser_radiance_history_s_1      = VirtualResourceID::DenoiserRadianceHistoryS1;
		HLSL::RWTexture2D<u32>   denoiser_radiance_history_d_1      = VirtualResourceID::DenoiserRadianceHistoryD1;
		HLSL::RWTexture2D<float> denoiser_accumulated_frame_count_1 = VirtualResourceID::DenoiserAccumulatedFrameCount1;
		HLSL::RWTexture2D<float> denoiser_penumbra_mask_1           = VirtualResourceID::DenoiserPenumbraMask1;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct LightingSpatialDenoiserRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float2>   ggx_preintegrated_brdf_lut         = VirtualResourceID::GgxPreintegratedBrdfLUT;
		HLSL::Texture2D<float>    depth_stencil                      = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float4>   gb_albedo_metalness                = VirtualResourceID::GBufferAlbedoMetalness;
		HLSL::Texture2D<float4>   gb_normal_roughness                = VirtualResourceID::GBufferNormalRoughness;
		HLSL::Texture2D<float>    denoiser_accumulated_frame_count_1 = VirtualResourceID::DenoiserAccumulatedFrameCount1;
		HLSL::Texture2D<float3>   denoiser_radiance_not_blurred_s    = VirtualResourceID::DenoiserRadianceHistoryS1;
		HLSL::Texture2D<float3>   denoiser_radiance_not_blurred_d    = VirtualResourceID::DenoiserRadianceHistoryD1;
		HLSL::Texture2D<float3>   denoiser_radiance_history_s_1      = VirtualResourceID::DenoiserRadianceHistoryS1;
		HLSL::Texture2D<float3>   denoiser_radiance_history_d_1      = VirtualResourceID::DenoiserRadianceHistoryD1;
		HLSL::Texture2D<float>    denoiser_penumbra_mask_1           = VirtualResourceID::DenoiserPenumbraMask1;
		HLSL::RWTexture2D<u32>    denoiser_radiance_history_s_0      = VirtualResourceID::DenoiserRadianceHistoryS0;
		HLSL::RWTexture2D<u32>    denoiser_radiance_history_d_0      = VirtualResourceID::DenoiserRadianceHistoryD0;
		HLSL::RWTexture2D<float4> scene_radiance                     = VirtualResourceID::SceneRadiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 pass_index;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::ShaderName{ "CloudVolume.hlsl"_sl })
enum struct CloudVolumeShaders : u32 {
	CloudEntityCulling   = 1u << 0,
	CloudCulling         = 1u << 1,
	BuildCloudUpdateList = 1u << 2,
	CompositeCloudVolume = 1u << 3,
	BuildCloudVolumeMask = 1u << 4,
};
SHADER_DEFINITION_GENERATED_CODE(CloudVolumeShaders);


NOTES(Meta::HlslFile{ "CloudData.hlsl"_sl })
struct CloudCullingConstants {
	compile_const u32 thread_group_size = 256u;
	
	compile_const uint3 grid_size_cells = CloudConstants::culling_volume_size;
	compile_const u32   grid_cell_count = grid_size_cells.x * grid_size_cells.y * grid_size_cells.z;
	
	compile_const u32 max_elements_per_cell = 8;
	compile_const u32 max_input_cloud_count = max_elements_per_cell * 32;
	compile_const u32 grid_element_count    = max_elements_per_cell * grid_cell_count;
	
	compile_const u32 culling_command_bin_count = 16;
	compile_const u32 culling_command_bin_size  = max_input_cloud_count;
	compile_const u32 culling_command_count     = Math::Max(culling_command_bin_size * culling_command_bin_count, grid_cell_count / 2);
	
	static_assert((1u << (culling_command_bin_count - 1)) == grid_cell_count);
};

NOTES(Meta::HlslFile{ "CloudData.hlsl"_sl })
enum struct CloudCullingIndirectArgumentsLayout : u32 {
	CloudCullingCommands,
	CloudCullingEnd = CloudCullingCommands + CloudCullingConstants::culling_command_bin_count - 1,
	
	CoarseCloudUpdateList,
	FineCloudUpdateList,
	
	Count
};

NOTES(Meta::RenderPass{})
struct CloudEntityCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32>                      cloud_volume_alive_mask    = VirtualResourceID::CloudVolumeAliveMask;
		HLSL::RegularBuffer<GpuCloudVolumeEntityData> cloud_volume_data          = VirtualResourceID::GpuCloudVolumeEntityData;
		HLSL::RWRegularBuffer<uint2>                  cloud_culling_commands     = VirtualResourceID::CloudCullingCommands;
		HLSL::RWRegularBuffer<uint4>                  indirect_arguments         = VirtualResourceID::CloudCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>                    texture_streaming_feedback = VirtualResourceID::TextureStreamingFeedback;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct CloudCullingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuCloudVolumeEntityData> cloud_volume_data      = VirtualResourceID::GpuCloudVolumeEntityData;
		HLSL::RegularBuffer<uint2>                    cloud_culling_commands = VirtualResourceID::CloudCullingCommands;
		HLSL::RWRegularBuffer<uint4>                  indirect_arguments     = VirtualResourceID::CloudCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>                    cloud_culling_grid     = VirtualResourceID::CloudCullingGrid;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			u32 bin_index = 0;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct BuildCloudUpdateListRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture3D<u64>         sdf_cloud_volume_mask = VirtualResourceID::SdfCloudVolumeMask;
		HLSL::RegularBuffer<u32>     cloud_culling_grid    = VirtualResourceID::CloudCullingGrid;
		HLSL::RWRegularBuffer<uint4> indirect_arguments    = VirtualResourceID::CloudCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>   cloud_update_list     = VirtualResourceID::CloudCullingCommands;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct CompositeCloudVolumeRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	WorldEntitySystem* world_system = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<GpuCloudVolumeEntityData> cloud_volume_data  = VirtualResourceID::GpuCloudVolumeEntityData;
		HLSL::RegularBuffer<u32>                      cloud_update_list  = VirtualResourceID::CloudCullingCommands;
		HLSL::RegularBuffer<u32>                      cloud_culling_grid = VirtualResourceID::CloudCullingGrid;
		HLSL::RWTexture3D<float>                      sdf_cloud_volume   = VirtualResourceID::SdfCloudVolume;
		HLSL::RWTexture3D<u32>                        sdf_cloud_volume_transient_mask = VirtualResourceID::SdfCloudVolumeTransientMask;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct BuildCloudVolumeMaskRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture3D<u32>         sdf_cloud_volume_transient_mask = VirtualResourceID::SdfCloudVolumeTransientMask;
		HLSL::RWTexture3D<u64>       sdf_cloud_volume_mask           = VirtualResourceID::SdfCloudVolumeMask;
		HLSL::RWRegularBuffer<uint4> indirect_arguments              = VirtualResourceID::CloudCullingIndirectArguments;
		HLSL::RWRegularBuffer<u32>   cloud_update_list               = VirtualResourceID::CloudCullingCommands;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "CloudRaymarch.hlsl"_sl })
enum struct CloudRaymarchShaders : u32 {
	CloudRaymarch          = 1u << 0,
	OpticalDepthVolume     = 1u << 1,
	ShadowMap              = 1u << 2,
	ShadowMapFilter        = 1u << 3,
	RadianceTransferVolume = 1u << 4,
};
SHADER_DEFINITION_GENERATED_CODE(CloudRaymarchShaders);

NOTES(Meta::RenderPass{})
struct CloudRaymarchRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float>       depth_stencil                  = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<float3>      sky_panorama_lut               = VirtualResourceID::SkyPanoramaLut;
		HLSL::Texture2D<float3>      average_sky_irradiance         = VirtualResourceID::AverageSkyIrradiance;
		HLSL::Texture2D<float3>      transmittance_lut              = VirtualResourceID::TransmittanceLut;
		HLSL::Texture3D<float>       sdf_cloud_volume               = VirtualResourceID::SdfCloudVolume;
		HLSL::Texture3D<u64>         sdf_cloud_volume_mask          = VirtualResourceID::SdfCloudVolumeMask;
		HLSL::Texture3D<float>       cloud_optical_depth_volume     = VirtualResourceID::CloudOpticalDepthVolume;
		HLSL::Texture3D<float2>      cloud_radiance_transfer_volume = VirtualResourceID::CloudRadianceTransferVolume0;
		HLSL::Texture2DArray<float>  blue_noise_1d                  = VirtualResourceID::BlueNoise1D;
		HLSL::Texture2DArray<float2> blue_noise_2d                  = VirtualResourceID::BlueNoise2D;
		HLSL::RWTexture2D<float4>    scene_radiance                 = VirtualResourceID::SceneRadiance;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct CloudOpticalDepthVolumeRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32>  cloud_update_list          = VirtualResourceID::CloudCullingCommands;
		HLSL::Texture3D<float>    sdf_cloud_volume           = VirtualResourceID::SdfCloudVolume;
		HLSL::Texture3D<u64>      sdf_cloud_volume_mask      = VirtualResourceID::SdfCloudVolumeMask;
		HLSL::RWTexture3D<float>  cloud_optical_depth_volume = VirtualResourceID::CloudOpticalDepthVolume;
		HLSL::RWTexture2D<float3> transient_cloud_shadow_map = VirtualResourceID::TransientCloudShadowMap;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id_optical_depth_volume;
	inline static PipelineID pipeline_id_shadow_map;
};

NOTES(Meta::RenderPass{})
struct CloudShadowMapFilterRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RWTexture2D<float3> transient_cloud_shadow_map = VirtualResourceID::TransientCloudShadowMap;
		HLSL::RWTexture2D<float3> cloud_shadow_map           = VirtualResourceID::CloudShadowMap1;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct CloudRadianceTransferVolumeRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<u32>  cloud_update_list                  = VirtualResourceID::CloudCullingCommands;
		HLSL::Texture3D<float>    sdf_cloud_volume                   = VirtualResourceID::SdfCloudVolume;
		HLSL::Texture3D<u64>      sdf_cloud_volume_mask              = VirtualResourceID::SdfCloudVolumeMask;
		HLSL::Texture3D<float>    cloud_optical_depth_volume         = VirtualResourceID::CloudOpticalDepthVolume;
		HLSL::Texture3D<float2>   cloud_radiance_transfer_volume_1   = VirtualResourceID::CloudRadianceTransferVolume1;
		HLSL::RWTexture3D<u32>    cloud_sample_count_volume          = VirtualResourceID::CloudSampleCountVolume;
		HLSL::RWTexture3D<float2> cloud_radiance_transfer_volume_0_0 = HLSL::RWTexture3D<float2>(VirtualResourceID::CloudRadianceTransferVolume0, 0);
		HLSL::RWTexture3D<float2> cloud_radiance_transfer_volume_0_1 = HLSL::RWTexture3D<float2>(VirtualResourceID::CloudRadianceTransferVolume0, 1);
		HLSL::RWTexture3D<float2> cloud_radiance_transfer_volume_0_2 = HLSL::RWTexture3D<float2>(VirtualResourceID::CloudRadianceTransferVolume0, 2);
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::ShaderName{ "AutomaticExposure.hlsl"_sl })
enum struct AutomaticExposureShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(AutomaticExposureShaders);

NOTES(Meta::HlslFile{ "ToneMappingData.hlsl"_sl })
struct AutomaticExposureGpuConstants {
	compile_const u32 histogram_bucket_count = ExposureSettings::histogram_bucket_count;
	compile_const u32 thread_group_size      = 16u;
	compile_const u32 thread_tile_size       = 8u;
	
	float2 ev_to_bucket_index     = 0.f;
	float2 bucket_index_to_ev     = 0.f;
	float histogram_min_ev        = 0.f;
	float histogram_min_cutoff    = 0.f;
	float histogram_max_cutoff    = 0.f;
	float histogram_min_luminance = 0.f;
	float exposure_min_ev         = 0.f;
	float exposure_max_ev         = 0.f;
	float exposure_increase_t     = 0.f;
	float exposure_decrease_t     = 0.f;
	float exposure_scale          = 0.f;
	u32 last_thread_group_index   = 0;
	ExposureMethod method         = ExposureMethod::Manual;
};

NOTES(Meta::RenderPass{})
struct AutomaticExposureRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	ExposureSettings exposure_settings;
	float delta_time = 0.f;
	GpuReadbackQueue* automatic_exposure_readback_queue = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float3>      scene_radiance      = VirtualResourceID::SceneRadiance;
		HLSL::RWRegularBuffer<u32>   luminance_histogram = VirtualResourceID::LuminanceHistogram;
		HLSL::RWRegularBuffer<float> exposure            = VirtualResourceID::Exposure;
		HLSL::RWTexture2D<float>     exposure_texture    = VirtualResourceID::ExposureTexture;
		HLSL::RWRegularBuffer<float> luminance_histogram_readback;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::ConstantBuffer<AutomaticExposureGpuConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::ShaderName{ "ToneMapping.hlsl"_sl })
enum struct ToneMappingShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(ToneMappingShaders);

NOTES(Meta::HlslFile{ "ToneMappingData.hlsl"_sl })
struct ToneMappingGpuConstants {
	compile_const float reference_luminance = 80.f; // Frame buffer value to cd/m^2.
	compile_const u32   thread_group_size   = 16u;
	
	ToneMappingMethod method = ToneMappingMethod::GT7_HDR;
	
	float framebuffer_luminance_target = 0.f;
	float sdr_correction_factor        = 0.f;
	
	float mid_point     = 0.f;
	float toe_threshold = 0.f;
	float toe_power     = 0.f;
	float k_a           = 0.f;
	float k_b           = 0.f;
	float k_c           = 0.f;
	
	float blend_ratio = 0.f;
	float fade_start  = 0.f;
	float fade_end    = 0.f;
	
	uint2 output_offset;
	u32 use_external_output;
};

NOTES(Meta::RenderPass{})
struct ToneMappingRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	ToneMappingSettings tone_mapping_settings;
	VirtualResourceID scene_radiance  = VirtualResourceID::None;
	VirtualResourceID external_output = VirtualResourceID::None;
	uint2 output_offset = 0;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::RegularBuffer<float> exposure = VirtualResourceID::Exposure;
		HLSL::RWTexture2D<float4> scene_radiance;
		HLSL::RWTexture2D<float4> external_output;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::ConstantBuffer<ToneMappingGpuConstants> constants;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};


NOTES(Meta::RenderPass{})
struct DlssRenderPass {
	RENDER_PASS_GENERATED_CODE();
	float2 jitter_offset_pixels;
	float exposure_estimate = 1.f;
};

NOTES(Meta::RenderPass{})
struct XessRenderPass {
	RENDER_PASS_GENERATED_CODE();
	float2 jitter_offset_pixels;
	float exposure_estimate = 1.f;
};


NOTES(Meta::ShaderName{ "DebugGeometry.hlsl"_sl })
enum struct DebugGeometryShaders : u32 {};
SHADER_DEFINITION_GENERATED_CODE(DebugGeometryShaders);

NOTES(Meta::HlslFile{ "DebugGeometryData.hlsl"_sl })
struct DebugGeometrySettings {
	compile_const u32 debug_mesh_instance_count = 16 * 1024;
};

NOTES(Meta::RenderPass{})
struct DebugGeometryClearBuffersRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	DebugGeometryBuffer* debug_geometry_buffer = nullptr;
};

NOTES(Meta::RenderPass{ CommandQueueType::Graphics })
struct DebugGeometryRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	ArrayView<DebugMeshInstanceArray> debug_mesh_instance_arrays;
	DebugGeometryBuffer* debug_geometry_buffer = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float> depth_stencil = VirtualResourceID::DepthStencil;
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

NOTES(Meta::ShaderName{ "DebugVisualization.hlsl"_sl })
enum struct DebugVisualizationShaders : u32 {
	DebugVisualization = 1u << 0,
	DebugReadback      = 1u << 1,
};
SHADER_DEFINITION_GENERATED_CODE(DebugVisualizationShaders);

NOTES(Meta::RenderPass{})
struct DebugVisualizationRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	VirtualResourceID scene_radiance = VirtualResourceID::None;
	DebugVisualizationMode mode = DebugVisualizationMode::None;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float>                 depth_stencil       = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<u32>                   visibility_buffer   = VirtualResourceID::VisibilityBuffer;
		HLSL::Texture2D<float4>                gb_albedo_metalness = VirtualResourceID::GBufferAlbedoMetalness;
		HLSL::Texture2D<float4>                gb_normal_roughness = VirtualResourceID::GBufferNormalRoughness;
		HLSL::RegularBuffer<GpuMeshEntityData> mesh_entity_data    = VirtualResourceID::GpuMeshEntityData;
		HLSL::ByteBuffer                       mesh_asset_buffer   = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<uint2>             visible_meshlets    = VirtualResourceID::VisibleMeshlets;
		HLSL::RWTexture2D<float4>              scene_radiance      = VirtualResourceID::None;
	};
	
	struct RootSignature : HLSL::BaseRootSignature {
		struct PushConstants {
			DebugVisualizationMode mode = DebugVisualizationMode::None;
		};
		
		HLSL::PushConstantBuffer<PushConstants> constants;
		HLSL::ConstantBuffer<SceneConstants> scene;
		HLSL::DescriptorTable<Descriptors> descriptor_table;
	};
	
	inline static PipelineID pipeline_id;
};

NOTES(Meta::RenderPass{})
struct DebugReadbackRenderPass {
	RENDER_PASS_GENERATED_CODE();
	
	GpuReadbackQueue* readback_queue = nullptr;
	
	struct Descriptors : HLSL::BaseDescriptorTable {
		HLSL::Texture2D<float>     depth_stencil       = VirtualResourceID::DepthStencil;
		HLSL::Texture2D<u32>       visibility_buffer   = VirtualResourceID::VisibilityBuffer;
		HLSL::Texture2D<float4>    gb_normal_roughness = VirtualResourceID::GBufferNormalRoughness;
		HLSL::ByteBuffer           mesh_asset_buffer   = VirtualResourceID::MeshAssetBuffer;
		HLSL::RegularBuffer<uint2> visible_meshlets    = VirtualResourceID::VisibleMeshlets;
		HLSL::RWByteBuffer         readback_buffer;
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
struct ImGuiTextureIdPushConstants { u32 packed = 0; };

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

