#include "Basic.hlsl"

groupshared uint gs_page_residency_mask[MeshletPageHeader::max_page_residency_mask_size];
groupshared uint gs_rtas_residency_mask[MeshletPageHeader::max_page_residency_mask_size];

compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	MeshletPageTableUpdateCommand page_table_command = page_table_commands[group_id];
	GpuMeshAssetData mesh_asset = mesh_asset_data[page_table_command.mesh_asset_index];
	if (mesh_asset.meshlet_group_buffer_offset == u32_max) return;
	
	uint page_residency_mask_size   = DivideAndRoundUp(mesh_asset.meshlet_page_count, 32u);
	uint page_residency_mask_offset = mesh_asset.meshlet_group_buffer_offset + mesh_asset.meshlet_group_count * sizeof(MeshletGroup);
	uint rtas_residency_mask_offset = page_residency_mask_offset + page_residency_mask_size * sizeof(uint);
	uint page_table_offset          = rtas_residency_mask_offset + page_residency_mask_size * sizeof(uint);
	
	if (thread_index < page_residency_mask_size) {
		gs_page_residency_mask[thread_index] = mesh_asset_buffer.Load(page_residency_mask_offset + thread_index * sizeof(uint));
		gs_rtas_residency_mask[thread_index] = mesh_asset_buffer.Load(rtas_residency_mask_offset + thread_index * sizeof(uint));
	}
	_Static_assert(MeshletPageHeader::max_page_residency_mask_size <= thread_group_size, "Page residency mask size is too large to be loaded using a single thread group.");
	
	GroupMemoryBarrierWithGroupSync();
	
	for (uint command_index = thread_index; command_index < page_table_command.page_command_count; command_index += thread_group_size) {
		MeshletPageUpdateCommand page_command = page_commands[page_table_command.page_command_offset + command_index];
		
		uint asset_page_index = page_command.asset_page_index;
		if (page_command.type == MeshletPageUpdateCommandType::PageIn) {
			InterlockedOr(gs_page_residency_mask[asset_page_index / 32u], 1u << (asset_page_index % 32u));
		} else if (page_command.type == MeshletPageUpdateCommandType::RtasIn) {
			InterlockedOr(gs_rtas_residency_mask[asset_page_index / 32u], 1u << (asset_page_index % 32u));
		} else if (page_command.type == MeshletPageUpdateCommandType::PageOut) {
			InterlockedAnd(gs_page_residency_mask[asset_page_index / 32u], ~(1u << (asset_page_index % 32u)));
			InterlockedAnd(gs_rtas_residency_mask[asset_page_index / 32u], ~(1u << (asset_page_index % 32u)));
		}
		
		uint runtime_page_offset = (uint)page_command.runtime_page_index * MeshletPageHeader::page_size;
		if (page_command.type == MeshletPageUpdateCommandType::PageIn) {
			mesh_asset_buffer.Store(page_table_offset + asset_page_index * sizeof(uint), runtime_page_offset);
		}
		
		if (page_command.type == MeshletPageUpdateCommandType::PageIn) {
			page_header_readback[page_command.readback_index] = mesh_asset_buffer.Load<MeshletPageHeader>(runtime_page_offset);
		}
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < page_residency_mask_size) {
		mesh_asset_buffer.Store(page_residency_mask_offset + thread_index * sizeof(uint), gs_page_residency_mask[thread_index]);
		mesh_asset_buffer.Store(rtas_residency_mask_offset + thread_index * sizeof(uint), gs_rtas_residency_mask[thread_index]);
	}
	
	for (uint meshlet_group_index = thread_index; meshlet_group_index < mesh_asset.meshlet_group_count; meshlet_group_index += thread_group_size) {
		uint meshlet_group_offset = mesh_asset.meshlet_group_buffer_offset + meshlet_group_index * sizeof(MeshletGroup);
		MeshletGroup group = mesh_asset_buffer.Load<MeshletGroup>(meshlet_group_offset);
		
		uint group_residency_mask = MeshletGroupResidencyMask::Page | MeshletGroupResidencyMask::RTAS;
		for (uint page_index = group.page_index; page_index < (group.page_index + group.page_count); page_index += 1) {
			if ((gs_page_residency_mask[page_index / 32u] & (1u << (page_index % 32u))) == 0u) group_residency_mask &= ~MeshletGroupResidencyMask::Page;
			if ((gs_rtas_residency_mask[page_index / 32u] & (1u << (page_index % 32u))) == 0u) group_residency_mask &= ~MeshletGroupResidencyMask::RTAS;
		}
		
		if (group.residency_mask != group_residency_mask) {
			mesh_asset_buffer.Store<u16>(meshlet_group_offset + MeshletGroup::offset_of_residency_mask, (u16)group_residency_mask);
		}
	}
}
