#include "Basic.hlsl"
#include "Generated/MeshData.hlsl"
#include "Generated/MeshletRtasData.hlsl"
#include "MeshletVertexDecoding.hlsl"

// See license for NvAPI structures and enums in THIRD_PARTY_LICENSES.md
// Translated from NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS in nvapi.h
struct MeshletRtasBuildIndirectArguments {
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
_Static_assert(sizeof(MeshletRtasBuildIndirectArguments) == 64, "Mismatching NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS size.");

// Translated from NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS in nvapi.h
struct MeshletBlasBuildIndirectArguments {
	u32 meshlet_count;
	u32 padding;
	u64 meshlet_addresses;
};
_Static_assert(sizeof(MeshletBlasBuildIndirectArguments) == 16, "Mismatching NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS size.");

// Translated from D3D12_RAYTRACING_INSTANCE_DESC in d3d12.h
struct BlasInstanceDesc {
	float3x4 model_to_world;
	u32 instance_id   : 24;
	u32 instance_mask : 8;
	u32 user_data     : 24;
	u32 flags         : 8;
	u64 blas_address;
};
_Static_assert(sizeof(BlasInstanceDesc) == 64, "Mismatching D3D12_RAYTRACING_INSTANCE_DESC size.");

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

using IndirectArgumentsLayout = MeshletRtasIndirectArgumentsLayout;

#if defined(MESHLET_RTAS_DECODE_VERTEX_BUFFER)
groupshared uint gs_scratch_vertex_buffer_offset;

[ThreadGroupSize(128, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint group_index = packed_group_indices[group_id];
	
	MeshletRtasDecodeVertexBufferInputs inputs = decode_vertex_buffer_inputs[group_index & 0xFFFF];
	uint meshlet_index = group_index >> 16;
	
	uint page_offset = inputs.runtime_page_index * MeshletPageHeader::page_size;
	MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
	
	uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader) + meshlet_index * sizeof(MeshletCullingData);
	MeshletCullingData meshlet_culling_data = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
	
	uint meshlet_header_offset = meshlet_culling_data_offset + meshlet_culling_data.meshlet_header_offset;
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	
	if (thread_index == 0) {
		uint scratch_allocation_size = meshlet.vertex_count * sizeof(float3);
		
		uint scratch_vertex_buffer_offset = 0;
		InterlockedAdd(rtas_indirect_arguments[IndirectArgumentsLayout::VertexBufferAllocator], scratch_allocation_size, scratch_vertex_buffer_offset);
		scratch_vertex_buffer_offset += constants.vertex_buffer_scratch_offset;
		
		scratch_buffer.Store(inputs.vertex_buffer_offsets + meshlet_index * sizeof(uint), scratch_vertex_buffer_offset);
		
		gs_scratch_vertex_buffer_offset = scratch_vertex_buffer_offset;
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < meshlet.vertex_count) {
		MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
		MeshletVertex vertex = LoadMeshletVertexBuffer(mesh_asset_buffer, offsets.vertex_buffer_offset, thread_index);
		
		GpuMeshAssetData mesh_asset = mesh_asset_data[inputs.mesh_asset_index];
		float3 vertex_position = DecodeMeshletVertexPosition(vertex.position, mesh_asset, meshlet);
		
		scratch_buffer.Store(gs_scratch_vertex_buffer_offset + thread_index * sizeof(float3), vertex_position);
	}
}
#endif // defined(MESHLET_RTAS_DECODE_VERTEX_BUFFER)


#if defined(MESHLET_RTAS_BUILD_INDIRECT_ARGUMENTS)
compile_const uint thread_group_size = 64;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	MeshletRtasBuildIndirectArgumentsInputs inputs = meshlet_rtas_inputs[group_id];
	
	uint page_offset = inputs.runtime_page_index * MeshletPageHeader::page_size;
	MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
	
	for (uint meshlet_index = thread_index; meshlet_index < page_header.meshlet_count; meshlet_index += thread_group_size) {
		uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader) + meshlet_index * sizeof(MeshletCullingData);
		MeshletCullingData meshlet_culling_data = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
		
		uint meshlet_header_offset = meshlet_culling_data_offset + meshlet_culling_data.meshlet_header_offset;
		MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
		
		MeshletBufferOffsets offsets = ComputeMeshletBufferOffsets(meshlet, meshlet_header_offset);
		uint scratch_vertex_buffer_offset = scratch_buffer.Load(inputs.vertex_buffer_offsets + meshlet_index * sizeof(uint));
		
		MeshletRtasBuildIndirectArguments arguments = (MeshletRtasBuildIndirectArguments)0;
		arguments.meshlet_id           = meshlet_header_offset;
		arguments.meshlet_flags        = MeshletFlags::None;
		arguments.triangle_count       = meshlet.triangle_count;
		arguments.vertex_count         = meshlet.vertex_count;
		arguments.index_format         = 1; // 8 bit indices.
		arguments.base_geometry_flags  = MeshletGeometryFlags::Opaque;
		arguments.index_buffer_stride  = 1;
		arguments.vertex_buffer_stride = sizeof(float3);
		arguments.index_buffer         = constants.mesh_asset_buffer_address + (u64)offsets.index_buffer_offset;
		arguments.vertex_buffer        = constants.scratch_buffer_address    + (u64)scratch_vertex_buffer_offset;
		scratch_buffer.Store(inputs.indirect_arguments_offset + meshlet_index * sizeof(MeshletRtasBuildIndirectArguments), arguments);
	}
}
#endif // defined(MESHLET_RTAS_BUILD_INDIRECT_ARGUMENTS)


#if defined(MESHLET_RTAS_WRITE_OFFSETS)
groupshared uint gs_page_meshlet_rtas_size;

compile_const uint thread_group_size = 64;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	MeshletRtasWriteOffsetsInputs inputs = write_offsets_inputs[group_id];
	
	uint page_offset = inputs.runtime_page_index * MeshletPageHeader::page_size;
	MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
	
	uint thread_page_meshlet_rtas_size = 0;
	for (uint meshlet_index = thread_index; meshlet_index < page_header.meshlet_count; meshlet_index += thread_group_size) {
		u64 meshlet_rtas_address = scratch_buffer.Load<u64>(inputs.meshlet_descs_offset + meshlet_index * 16u);
		u32 meshlet_rtas_size    = scratch_buffer.Load<u32>(inputs.meshlet_descs_offset + 8u + meshlet_index * 16u);
		u32 meshlet_rtas_offset  = (u32)(meshlet_rtas_address - constants.meshlet_rtas_buffer_address);
		
		uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader) + meshlet_index * sizeof(MeshletCullingData);
		MeshletCullingData meshlet_culling_data = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
		
		uint meshlet_header_offset = meshlet_culling_data_offset + meshlet_culling_data.meshlet_header_offset;
		MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
		
		meshlet.rtas_offset = meshlet_rtas_offset;
		mesh_asset_buffer.Store(meshlet_header_offset, meshlet);
		
		thread_page_meshlet_rtas_size += meshlet_rtas_size;
	}
	
	
	if (thread_index == 0) {
		gs_page_meshlet_rtas_size = 0;
	}
	
	GroupMemoryBarrierWithGroupSync();
	InterlockedAdd(gs_page_meshlet_rtas_size, thread_page_meshlet_rtas_size);
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index == 0) {
		page_size_readback.Store(group_id * sizeof(u32), gs_page_meshlet_rtas_size);
	}
}
#endif // defined(MESHLET_RTAS_WRITE_OFFSETS)


#if defined(MESHLET_RTAS_UPDATE_OFFSETS)
groupshared uint gs_output_base_index;

compile_const uint thread_group_size = 64;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	MeshletRtasUpdateOffsetsInputs inputs = update_offsets_inputs[group_id];
	
	uint page_offset = inputs.runtime_page_index * MeshletPageHeader::page_size;
	MeshletPageHeader page_header = mesh_asset_buffer.Load<MeshletPageHeader>(page_offset);
	
	if (thread_index == 0) {
		InterlockedAdd(rtas_indirect_arguments[IndirectArgumentsLayout::CompactionMoveCount], page_header.meshlet_count, gs_output_base_index);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	for (uint meshlet_index = thread_index; meshlet_index < page_header.meshlet_count; meshlet_index += thread_group_size) {
		uint meshlet_culling_data_offset = page_offset + sizeof(MeshletPageHeader) + meshlet_index * sizeof(MeshletCullingData);
		MeshletCullingData meshlet_culling_data = mesh_asset_buffer.Load<MeshletCullingData>(meshlet_culling_data_offset);
		
		uint meshlet_header_offset = meshlet_culling_data_offset + meshlet_culling_data.meshlet_header_offset;
		MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
		
		u32 old_offset = meshlet.rtas_offset;
		u32 new_offset = meshlet.rtas_offset - inputs.page_address_shift;
		
		u64 old_address = constants.meshlet_rtas_buffer_address + (u64)old_offset;
		u64 new_address = constants.meshlet_rtas_buffer_address + (u64)new_offset;
		
		u32 meshlet_offset = (gs_output_base_index + meshlet_index) * sizeof(u64);
		scratch_buffer.Store<u64>(constants.new_addresses_offset + meshlet_offset, new_address);
		scratch_buffer.Store<u64>(constants.old_addresses_offset + meshlet_offset, old_address);
		
		meshlet.rtas_offset = new_offset;
		mesh_asset_buffer.Store(meshlet_header_offset, meshlet);
	}
}
#endif // defined(MESHLET_RTAS_UPDATE_OFFSETS)


#if defined(MESHLET_BLAS_BUILD_INDIRECT_ARGUMENTS)
compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint meshlet_count = instance_meshlet_counts[thread_id];
	
	uint blas_desc_offset = u32_max;
	if (meshlet_count != 0 && meshlet_count <= MeshletConstants::max_meshlets_per_blas) {
		uint meshlet_list_offset = 0;
		InterlockedAdd(rtas_indirect_arguments[IndirectArgumentsLayout::BlasMeshletCount], meshlet_count, meshlet_list_offset);
		
		uint candidate_blas_index = 0;
		InterlockedAdd(rtas_indirect_arguments[IndirectArgumentsLayout::CandidateBlasCount], 1u, candidate_blas_index);
		
		if (candidate_blas_index < MeshletConstants::max_meshlet_blas_count && (meshlet_list_offset + meshlet_count) <= MeshletConstants::max_total_blas_meshlets) {
			uint committed_blas_index = 0;
			InterlockedAdd(rtas_indirect_arguments[IndirectArgumentsLayout::CommittedBlasCount], 1u, committed_blas_index);
			
			blas_desc_offset               = MeshletConstants::blas_build_result_blas_descs_offset  + committed_blas_index * sizeof(MeshletBlasBuildIndirectArguments);
			uint indirect_arguments_offset = MeshletConstants::blas_build_indirect_arguments_offset + committed_blas_index * 16u;
			uint meshlet_addresses_offset  = MeshletConstants::blas_build_meshlet_addresses_offset  + meshlet_list_offset  * sizeof(u64);
			
			MeshletBlasBuildIndirectArguments arguments = (MeshletBlasBuildIndirectArguments)0;
			arguments.meshlet_count     = meshlet_count;
			arguments.meshlet_addresses = constants.scratch_buffer_address + (u64)meshlet_addresses_offset;
			scratch_buffer.Store(indirect_arguments_offset, arguments);
			
			scratch_buffer.Store(blas_desc_offset + sizeof(uint3), meshlet_addresses_offset);
		}
	}
	
	instance_meshlet_counts[thread_id] = blas_desc_offset;
}
#endif // defined(MESHLET_BLAS_ALLOCATE_ADDRESSES)


#if defined(MESHLET_BLAS_WRITE_ADDRESSES)
compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint total_meshlet_count = indirect_arguments[MeshletCullingIndirectArgumentsLayout::RaytracingBuildBLAS].w;
	if (thread_id >= total_meshlet_count) return;
	
	uint2 meshlet_instance = visible_meshlets[thread_id]; 
	
	uint meshlet_header_offset = meshlet_instance.x;
	uint mesh_entity_index     = meshlet_instance.y;
	
	uint blas_desc_offset = instance_meshlet_counts[mesh_entity_index];
	if (blas_desc_offset == u32_max) return;
	
	uint meshlet_offset = 0;
	scratch_buffer.InterlockedAdd(blas_desc_offset + sizeof(uint3), sizeof(u64), meshlet_offset);
	
	MeshletHeader meshlet = mesh_asset_buffer.Load<MeshletHeader>(meshlet_header_offset);
	u64 meshlet_rtas_address = constants.meshlet_rtas_buffer_address + (u64)meshlet.rtas_offset;
	
	scratch_buffer.Store<u64>(meshlet_offset, meshlet_rtas_address);
}
#endif // defined(MESHLET_BLAS_WRITE_ADDRESSES)


#if defined(BUILD_MESH_ENTITY_INSTANCES)
compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint thread_id : SV_DispatchThreadID) {
	uint mesh_entity_index = thread_id;
	if (BitArrayTestBit(mesh_alive_mask, mesh_entity_index) == false) return;
	
	u32 blas_desc_offset = instance_meshlet_counts[mesh_entity_index];
	u64 blas_address = blas_desc_offset != u32_max ? scratch_buffer.Load<u64>(blas_desc_offset) : 0;
	
	uint instance_index = 0;
	InterlockedAdd(rtas_indirect_arguments[IndirectArgumentsLayout::TlasMeshInstanceCount], 1u, instance_index);
	
	BlasInstanceDesc desc = (BlasInstanceDesc)0;
	desc.instance_id    = mesh_entity_index;
	desc.instance_mask  = blas_address ? 0xFF : 0;
	desc.user_data      = 0;
	desc.flags          = 0;
	desc.blas_address   = blas_address;
	
	GpuTransform model_to_world = mesh_transforms[mesh_entity_index];
	
	float3x3 model_to_world_rotation = QuatToRotationMatrix(model_to_world.rotation);
	desc.model_to_world[0] = float4(model_to_world_rotation[0] * model_to_world.scale, model_to_world.position[0]);
	desc.model_to_world[1] = float4(model_to_world_rotation[1] * model_to_world.scale, model_to_world.position[1]);
	desc.model_to_world[2] = float4(model_to_world_rotation[2] * model_to_world.scale, model_to_world.position[2]);
	
	tlas_mesh_instances.Store(instance_index * sizeof(BlasInstanceDesc), desc);
}
#endif // defined(BUILD_MESH_ENTITY_INSTANCES)
