#include "Basic.hlsl"
#include "Generated/MeshData.hlsl"

#if defined(CLEAR_BUFFERS)
[ThreadGroupSize(1, 1, 1)]
void MainCS() {
	scratch_buffer.Store(constants.scratch_allocator_offset, constants.scratch_allocator_base);
}
#endif // defined(CLEAR_BUFFERS)


#if defined(MESHLET_RTAS_DECODE_VERTEX_BUFFER)
groupshared uint gs_scratch_vertex_buffer_offset;

[ThreadGroupSize(128, 1, 1)]
void MainCS(uint meshlet_index : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint page_offset = constants.runtime_page_index * MeshletPageHeader::page_size;
	MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
	
	uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader) + meshlet_index * sizeof(MeshletCullingData);
	MeshletCullingData meshlet_culling_data = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
	
	uint meshlet_header_offset = meshlet_culling_data_offset + meshlet_culling_data.meshlet_header_offset;
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	
	if (thread_index == 0) {
		uint scratch_allocation_size = meshlet.vertex_count * sizeof(float3);
		scratch_buffer.InterlockedAdd(constants.scratch_allocator_offset, scratch_allocation_size, gs_scratch_vertex_buffer_offset);
		
		scratch_buffer.Store(constants.vertex_buffer_offsets + meshlet_index * sizeof(uint), gs_scratch_vertex_buffer_offset);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < meshlet.vertex_count) {
		uint vertex_buffer_offset = meshlet_header_offset + sizeof(MeshletHeader);
		MeshletVertex vertex = mesh_asset_buffer.Load<MeshletVertex>(vertex_buffer_offset + thread_index * sizeof(MeshletVertex));
		
		GpuMeshAssetData mesh_asset = mesh_asset_data[constants.mesh_asset_index];
		float3 vertex_position = float3(vertex.position) * mesh_asset.rcp_quantization_scale + meshlet.position_offset;
		
		scratch_buffer.Store(gs_scratch_vertex_buffer_offset + thread_index * sizeof(float3), vertex_position);
	}
}
#endif // defined(MESHLET_RTAS_DECODE_VERTEX_BUFFER)


#if defined(MESHLET_RTAS_BUILD_INDIRECT_ARGUMENTS)

// See license for NvAPI structures and enums in THIRD_PARTY_LICENSES.md
// Translated from NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS in nvapi.h
struct IndirectArguments {
	u32 meshlet_id;
	u32 meshlet_flags;
	u32 triangle_count                : 9;
	u32 vertex_count                  : 9;
	u32 position_truncate_bit_count   : 6;
	u32 index_format                  : 4;
	u32 opacity_micromap_index_format : 4;
	u32 base_geometry_index           : 24;
	u32 base_geometry_flags           : 8;
	u16 index_buffer_stride;
	u16 vertex_buffer_stride;
	u16 geometry_index_and_flags_buffer_stride;
	u16 opacity_micromap_index_buffer_stride;
	u64 index_buffer;
	u64 vertex_buffer;
	u64 geometry_index_and_flags_buffer;
	u64 opacity_micromap_array;
	u64 opacity_micromap_index_buffer;
};
_Static_assert(sizeof(IndirectArguments) == 64, "Mismatching NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS size.");

// Translated from NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_CLUSTER_FLAGS in nvapi.h
enum struct MeshletFlags : u32 {
	None             = 0u,
	AllowDisableOMMs = 1u << 0,
};

// Translated from NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAGS in nvapi.h, shifted down by 24 bits (geometry index).
enum struct MeshletGeometryFlags : u32 {
	None                        = 0u,
	CullDisable                 = 1u << 5,
	NoDuplicateAnyHitInvocation = 1u << 6,
	Opaque                      = 1u << 7,
};

compile_const uint thread_group_size = 64;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	MeshletRtasBuildIndirectArgumentsInputs inputs = meshlet_rtas_inputs[group_id];
	
	uint page_offset = inputs.page_index * MeshletPageHeader::page_size;
	MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
	
	for (uint meshlet_index = thread_index; meshlet_index < page_header.meshlet_count; meshlet_index += thread_group_size) {
		uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader) + meshlet_index * sizeof(MeshletCullingData);
		MeshletCullingData meshlet_culling_data = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
		
		uint meshlet_header_offset = meshlet_culling_data_offset + meshlet_culling_data.meshlet_header_offset;
		MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
		
		uint vertex_buffer_offset = meshlet_header_offset + sizeof(MeshletHeader);
		uint index_buffer_offset  = vertex_buffer_offset + meshlet.vertex_count * sizeof(MeshletVertex);
		
		uint scratch_vertex_buffer_offset = scratch_buffer.Load(inputs.vertex_buffer_offsets + meshlet_index * sizeof(uint));
		
		IndirectArguments arguments = (IndirectArguments)0;
		arguments.meshlet_id           = meshlet_header_offset;
		arguments.meshlet_flags        = MeshletFlags::None;
		arguments.triangle_count       = meshlet.triangle_count;
		arguments.vertex_count         = meshlet.vertex_count;
		arguments.index_format         = 1; // 8 bit indices.
		arguments.base_geometry_flags  = MeshletGeometryFlags::Opaque;
		arguments.index_buffer_stride  = 1;
		arguments.vertex_buffer_stride = sizeof(float3);
		arguments.index_buffer         = constants.mesh_asset_buffer_address + (u64)index_buffer_offset;
		arguments.vertex_buffer        = constants.scratch_buffer_address    + (u64)scratch_vertex_buffer_offset;
		scratch_buffer.Store(inputs.indirect_arguments_offset + meshlet_index * sizeof(IndirectArguments), arguments);
	}
}
#endif // defined(MESHLET_RTAS_BUILD_INDIRECT_ARGUMENTS)
