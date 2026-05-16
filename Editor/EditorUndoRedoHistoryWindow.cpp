#include "Basic/Basic.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"
#include "LevelEditor.h"

static void DrawUndoRedoCommandRow(UndoRedoCommand& command) {
	compile_const char* command_type_names[3] = { "LoadEntityState", "CreateEntity", "RemoveEntity" };
	
	ImGui::TableNextRow();
	if (ImGui::TableSetColumnIndex(0)) ImGui::Text("0x%016llX", command.entity_guid);
	if (ImGui::TableSetColumnIndex(1)) ImGui::Text("%u", command.group_index);
	if (ImGui::TableSetColumnIndex(2)) ImGui::Text("%llu", command.offset);
	if (ImGui::TableSetColumnIndex(3)) ImGui::Text("%llu", command.size);
	if (ImGui::TableSetColumnIndex(4)) ImGui::TextUnformatted(command_type_names[(u32)command.command_type]);
}

void EditorUndoRedoHistoryWindow(UndoRedoSystem& undo_redo_system) {
	ImGui::Begin("Undo/Redo History");
	defer{ ImGui::End(); };
	
	ImGui::Text("Undo/Redo Buffer Size: %llu", undo_redo_system.undo_buffer.save_load_buffer.data.count + undo_redo_system.redo_buffer.save_load_buffer.data.count);
	
	auto table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("UndoRedoHistory", 5, table_flags) == false) return;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("GUID",    ImGuiTableColumnFlags_WidthStretch, 4.f);
	ImGui::TableSetupColumn("GroupID", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("Offset",  ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableSetupColumn("Size",    ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthStretch, 3.f);
	ImGui::TableHeadersRow();
	
	auto& redo_commands = undo_redo_system.redo_buffer.commands;
	for (s64 i = 0; i < (s64)redo_commands.count; i += 1) {
		DrawUndoRedoCommandRow(redo_commands[i]);
	}
	
	ImGui::Separator();
	ImGui::TableNextRow();
	if (ImGui::TableSetColumnIndex(0)) {
		ImGui::CollapsingHeader("vvv Undo Commands vvv | ^^^ Redo Commands ^^^", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns | ImGuiTreeNodeFlags_Leaf);
	}
	
	auto& undo_commands = undo_redo_system.undo_buffer.commands;
	for (s64 i = undo_commands.count - 1; i >= 0; i -= 1) {
		DrawUndoRedoCommandRow(undo_commands[i]);
	}
}
