#include "Basic/Basic.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/ShaderCompiler.h"
#include "LevelEditor.h"


void EditorShaderStatisticsWindow(StackAllocator* alloc, GraphicsContext* graphics_context) {
	ImGui::Begin("Shader Statistics");
	defer{ ImGui::End(); };
	
	TempAllocationScope(alloc);
	
	auto statistics = GetShaderCompilerStatistics(graphics_context->shader_compiler, alloc);
	
	ImGui::Text("Dirty Pipeline Count: %u", statistics.dirty_pipeline_count);
	
	auto table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("ShaderStatistics", 2, table_flags) == false) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch, 6.f);
	ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableHeadersRow();
	
	for (auto& pipeline : statistics.pipeline_statistics) {
		ImGui::TableNextRow();
		
		if (ImGui::TableSetColumnIndex(0)) {
			ImGui::TextUnformatted(pipeline.name.data);
		}
		
		if (ImGui::TableSetColumnIndex(1)) {
			ImGui::TextUnformatted(pipeline.is_dirty ? "Fail" : "Success");
		}
	}
}
