#include "Basic/Basic.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/ShaderCompiler.h"
#include "LevelEditor.h"

enum struct ShaderStatisticsColumnID : u32 {
	Name   = 0,
	Status = 1,
	
	Count
};

void EditorShaderStatisticsWindow(StackAllocator* alloc, GraphicsContext* graphics_context) {
	TempAllocationScope(alloc);
	
	auto statistics = GetShaderCompilerStatistics(graphics_context->shader_compiler, alloc);
	if (statistics.dirty_pipeline_count != 0) {
		ImGui::BeginMainMenuBarEx();
		
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImVec4(0.9f, 0.07f, 0.14f, 1.f), "Failed Pipeline Count: %u", statistics.dirty_pipeline_count);
		
		ImGui::EndMainMenuBarEx();
	}
	
	ImGui::Begin("Shader Statistics");
	defer{ ImGui::End(); };
	
	ImGui::Text("Failed Pipeline Count: %u", statistics.dirty_pipeline_count);
	
	auto table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti;
	if (ImGui::BeginTable("ShaderStatistics", (u32)ShaderStatisticsColumnID::Count, table_flags) == false) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch, 6.f, (u32)ShaderStatisticsColumnID::Name);
	ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.f, (u32)ShaderStatisticsColumnID::Status);
	ImGui::TableHeadersRow();
	
	if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
		HeapSort(statistics.pipeline_statistics, [&](const PipelineStateStatistics& lh, const PipelineStateStatistics& rh)-> bool {
			s32 delta = 0;
			for (s32 i = 0; i < sort_specs->SpecsCount && delta == 0; i += 1) {
				auto& spec = sort_specs->Specs[i];
				
				switch ((ShaderStatisticsColumnID)spec.ColumnUserID) {
				case ShaderStatisticsColumnID::Name:   delta = strcmp(lh.name.data, rh.name.data);  break;
				case ShaderStatisticsColumnID::Status: delta = (s32)lh.is_dirty - (s32)rh.is_dirty; break;
				default: DebugAssertAlways("Unexpected ColumnUserID '%'.", spec.ColumnUserID);      break;
				}
				
				delta *= (spec.SortDirection == ImGuiSortDirection_Ascending ? +1 : -1);
			}
			return delta < 0;
		});
	}
	
	for (u32 i = 0; i < statistics.pipeline_statistics.count; i += 1) {
		auto& pipeline = statistics.pipeline_statistics[i];
		
		ImGui::TableNextRow();
		
		ImGuiScopeID(i);
		
		if (ImGui::TableSetColumnIndex((u32)ShaderStatisticsColumnID::Name)) {
			ImGui::Bullet();
			ImGui::SameLine();
			
			ImGui::Selectable(pipeline.name.data, false, ImGuiSelectableFlags_SpanAllColumns);
		}
		
		if (ImGui::TableSetColumnIndex((u32)ShaderStatisticsColumnID::Status)) {
			ImGui::TextUnformatted(pipeline.is_dirty ? "Fail" : "Success");
		}
	}
}
