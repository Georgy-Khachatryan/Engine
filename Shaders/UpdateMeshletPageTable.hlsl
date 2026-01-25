#include "Basic.hlsl"

compile_const uint page_residency_mask_size = MeshletPageHeader::max_page_count / 32u;
groupshared uint gs_page_residency_mask[page_residency_mask_size];

compile_const uint thread_group_size = 256;

[ThreadGroupSize(thread_group_size, 1, 1)]
void MainCS(uint2 thread_id : SV_DispatchThreadID) {
	MeshletPageTableUpdateCommand command = commands[thread_id.y];
	GpuMeshAssetData mesh_asset = mesh_asset_data[command.mesh_asset_index];
	
	uint page_residency_mask_offset = mesh_asset.meshlet_group_buffer_offset + mesh_asset.meshlet_group_count * sizeof(MeshletGroup);
	if (thread_id.x < page_residency_mask_size) {
		gs_page_residency_mask[thread_id.x] = mesh_asset_buffer.Load(page_residency_mask_offset + thread_id.x * sizeof(uint));
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	uint page_command = 0;
	
	bool thread_has_page_command = (thread_id.x < command.page_list_count);
	if (thread_has_page_command) {
		page_command = page_list[command.page_list_offset + thread_id.x];
		_Static_assert(MeshletPageHeader::max_page_count <= thread_group_size, "Thread group might not process the whole page list.");
		
		uint page_index   = (page_command & 0x7FFFFFFF);
		uint command_type = (page_command >> 31);
		
		if (command_type == MeshletPageTableUpdateCommandType::PageIn) {
			InterlockedOr(gs_page_residency_mask[page_index / 32u], 1u << (page_index % 32u));
		} else if (command_type == MeshletPageTableUpdateCommandType::PageOut) {
			InterlockedAnd(gs_page_residency_mask[page_index / 32u], ~(1u << (page_index % 32u)));
		}
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_id.x < page_residency_mask_size) {
		mesh_asset_buffer.Store(page_residency_mask_offset + thread_id.x * sizeof(uint), gs_page_residency_mask[thread_id.x]);
	}
	
	for (uint meshlet_group_index = thread_id.x; meshlet_group_index < mesh_asset.meshlet_group_count; meshlet_group_index += thread_group_size) {
		uint meshlet_group_offset = mesh_asset.meshlet_group_buffer_offset + meshlet_group_index * sizeof(MeshletGroup);
		MeshletGroup group = mesh_asset_buffer.Load<MeshletGroup>(meshlet_group_offset);
		
		bool all_pages_resident = true;
		for (uint page_index = group.page_index; page_index < (group.page_index + group.page_count); page_index += 1) {
			all_pages_resident &= (gs_page_residency_mask[page_index / 32u] & (1u << (page_index % 32u))) != 0u;
		}
		
		bool group_is_set_resident = (group.is_resident != 0);
		if (group_is_set_resident != all_pages_resident) {
			mesh_asset_buffer.Store<u16>(meshlet_group_offset + MeshletGroup::offset_of_is_resident, (u16)(all_pages_resident ? 1 : 0));
		}
	}
}
