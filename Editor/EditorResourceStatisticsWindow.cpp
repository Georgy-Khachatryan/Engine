#include "Basic/Basic.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "LevelEditor.h"

enum struct ResourceStatisticsColumnID : u32 {
	Name = 0,
	Type = 1,
	Size = 2,
	
	Count
};

struct GraphicsResourceStatisics {
	String name;
	VirtualResource::Type type = VirtualResource::Type::None;
	u32 size = 0;
};

void EditorResourceStatisticsWindow(StackAllocator* alloc, VirtualResourceTable* resource_table) {
	TempAllocationScope(alloc);
	
	bool is_window_open = ImGui::Begin("Graphics Resources");
	defer{ ImGui::End(); };
	
	if (is_window_open == false) return;
	
	
	Array<GraphicsResourceStatisics> resources;
	ArrayReserve(resources, alloc, resource_table->virtual_resources.count);
	
	u64 total_size = 0;
	
	extern ArrayView<String> virtual_resource_id_names;
	for (u32 resource_index = 0; resource_index < resource_table->virtual_resources.count; resource_index += 1) {
		auto& src_resource = resource_table->virtual_resources[resource_index];
		
		u32 size = 0;
		if (src_resource.type == VirtualResource::Type::VirtualBuffer) {
			size = src_resource.buffer.allocated_size;
		} else if (src_resource.type == VirtualResource::Type::VirtualTexture) {
			auto texture_size = src_resource.texture.allocated_size;
			auto format = texture_format_info_map[(u32)texture_size.format];
			
			for (u32 mip_index = 0; mip_index < texture_size.mips; mip_index += 1) {
				uint3 mip_size_pixels = Math::Max(uint3(texture_size.x, texture_size.y, texture_size.DepthSliceCount()) >> mip_index, 1u);
				uint3 mip_size_blocks = Math::Max(mip_size_pixels >> uint3(format.block_size_log2, 0u), 1u);
				size += mip_size_blocks.x * mip_size_blocks.y * mip_size_blocks.z * format.block_size_bytes;
			}
			
			size *= texture_size.ArraySliceCount();
		}
		
		if (size != 0) {
			auto& dst_resource = ArrayEmplace(resources);
			dst_resource.name = virtual_resource_id_names[resource_index];
			dst_resource.type = src_resource.type;
			dst_resource.size = size;
			
			total_size += size;
		}
	}
	
	
	ImGui::Text("Total Size: %.1f MB", total_size / (1024.0 * 1024.0));
	
	auto table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti;
	if (ImGui::BeginTable("GraphicsResources", (u32)ResourceStatisticsColumnID::Count, table_flags) == false) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.f, (u32)ResourceStatisticsColumnID::Name);
	ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 2.f, (u32)ResourceStatisticsColumnID::Type);
	ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 2.f, (u32)ResourceStatisticsColumnID::Size);
	ImGui::TableHeadersRow();
	
	if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
		HeapSort<GraphicsResourceStatisics>(resources, [&](const GraphicsResourceStatisics& lh, const GraphicsResourceStatisics& rh)-> bool {
			s64 delta = 0;
			for (s32 i = 0; i < sort_specs->SpecsCount && delta == 0; i += 1) {
				auto& spec = sort_specs->Specs[i];
				
				switch ((ResourceStatisticsColumnID)spec.ColumnUserID) {
				case ResourceStatisticsColumnID::Name: delta = StringCompare(lh.name, rh.name); break;
				case ResourceStatisticsColumnID::Type: delta = (s64)lh.type - (s64)rh.type;     break;
				case ResourceStatisticsColumnID::Size: delta = (s64)lh.size - (s64)rh.size;     break;
				default: DebugAssertAlways("Unexpected ColumnUserID '%'.", spec.ColumnUserID);  break;
				}
				
				delta *= (spec.SortDirection == ImGuiSortDirection_Ascending ? +1 : -1);
			}
			return delta < 0;
		});
	}
	
	for (u32 i = 0; i < resources.count; i += 1) {
		auto& resource = resources[i];
		
		ImGui::TableNextRow();
		
		ImGuiScopeID(i);
		
		if (ImGui::TableSetColumnIndex((u32)ResourceStatisticsColumnID::Name)) {
			ImGui::Bullet();
			ImGui::SameLine();
			
			ImGui::Selectable(resource.name.data, false, ImGuiSelectableFlags_SpanAllColumns);
		}
		
		if (ImGui::TableSetColumnIndex((u32)ResourceStatisticsColumnID::Type)) {
			auto name = resource.type == VirtualResource::Type::VirtualBuffer ? "Buffer"_sl : "Texture"_sl;
			ImGui::TextUnformatted(name.data);
		}
		
		if (ImGui::TableSetColumnIndex((u32)ResourceStatisticsColumnID::Size)) {
			if (resource.size < 1024) {
				ImGui::Text("%u B", resource.size);
			} else if (resource.size < 1024 * 1024) {
				ImGui::Text("%.1f KB", resource.size / 1024.0);
			} else if (resource.size < 1024 * 1024 * 1024) {
				ImGui::Text("%.1f MB", resource.size / (1024.0 * 1024.0));
			}
		}
	}
}
