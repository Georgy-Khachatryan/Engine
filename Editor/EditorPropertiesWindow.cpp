#include "Basic/Basic.h"
#include "LevelEditor.h"
#include "EditorEntities.h"
#include "Engine/ImGuiCustomWidgets.h"
#include "Engine/UndoRedoSystem.h"

static void SharedComponentEntityView(StackAllocator* alloc, EntitySystemBase& entity_system, SharedEntityEditorQuery entity) {
	if (entity.guid) {
		auto guid_string = StringFormat(alloc, "0x%"_sl, (void*)entity.guid->guid);
		ImGui::TableInputText("GUID", guid_string, nullptr);
	}
	
	if (entity.name) {
		ImGui::TableInputText("Name", entity.name->name, &entity_system.heap);
	}
}

static void WorldComponentEntityView(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, WorldEntityEditorQuery entity) {
	if (entity.position) {
		auto& position = entity.position->position;
		ImGui::TableDragFloatWithReset("Position", &position.x, 3, 0.1f);
	}
	
	if (entity.rotation) {
		auto& rotation = entity.rotation->rotation;
		
		auto euler_angles = Math::QuatToEulerXyzAngles(rotation) * Math::radians_to_degrees;
		if (ImGui::TableDragFloatWithReset("Rotation", &euler_angles.x, 3, 1.f)) {
			euler_angles.y = Math::Clamp(euler_angles.y, -90.f, 90.f);
			rotation = Math::EulerXyzAnglesToQuat(euler_angles * Math::degrees_to_radians);
		}
	}
	
	if (entity.scale) {
		const char* label = "S"; // Reset scale.
		const float default_values = 1.f;
		ImGui::TableDragFloatWithReset("Scale", &entity.scale->scale, 1, 0.01f, 0.f, 8.f, "%.3f", 0, &label, &default_values);
	}
	
	if (entity.mesh_asset) {
		ImGui::TableEntityComboBox(alloc, "Mesh Asset", &asset_system, &entity.mesh_asset->guid, ECS::GetEntityTypeID<MeshAssetType>::id);
	}
	
	if (entity.material_asset) {
		ImGui::TableEntityComboBox(alloc, "Material Asset", &asset_system, &entity.material_asset->guid, ECS::GetEntityTypeID<MaterialAssetType>::id);
	}
	
	if (entity.camera) {
		ImGui::TableCombo("Camera Transform Type", (s32*)&entity.camera->transform_type, "Perspective\0Orthographic\0");
		ImGui::TableSliderFloat("Vertical Field Of View", &entity.camera->vertical_fov_degrees, 10.f, 135.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::TableSliderFloat("Camera Near Depth", &entity.camera->near_depth, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	}
	
	if (entity.light) {
		const char* label = "R"; // Reset.
		const float default_values = 1.f;
		ImGui::TableDragFloatWithReset("Irradiance", &entity.light->irradiance, 1, 0.1f, 0.f, 2000.f, "%.1f", 0, &label, &default_values);
		if (ImGui::BeginTableItem("Color")) {
			ImGui::ColorEdit3("", &entity.light->color.x, ImGuiColorEditFlags_Float);
			ImGui::EndTableItem();
		}
	}
	
	if (entity.light_entity) {
		ImGui::TableEntityComboBox(alloc, "Light Entity", &world_system, &entity.light_entity->guid, ECS::GetEntityTypeID<LightEntityType>::id);
	}
	
	if (entity.renderer_world) {
		auto& renderer_world = *entity.renderer_world;
		
		auto world_size_string = StringFormat(alloc, "%.x%"_sl, (u32)renderer_world.window_size.x, (u32)renderer_world.window_size.y);
		ImGui::TableInputText("Window Size", world_size_string, nullptr);
		
		ImGui::TableSliderFloat("Meshlet Target Error Pixels", &renderer_world.meshlet_target_error_pixels, 1.f, 128.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		
		if (ImGui::BeginTableItem("Freeze Culling State")) {
			ImGui::Checkbox("", &renderer_world.debug_freeze_culling_camera.enabled);
			ImGui::EndTableItem();
		}
		
		ImGui::TableSliderFloat("Reference Path Tracer Percent", &renderer_world.reference_path_tracer_percent, 0.f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		
		if (ImGui::BeginTableItem("Reset Reference Path Tracer")) {
			ImGui::SetNextItemShortcut(ImGuiKey_R, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused | ImGuiInputFlags_Repeat);
			renderer_world.reset_reference_path_tracer |= ImGui::Button("Reset", ImVec2(ImGui::GetContentRegionAvail().x, 0.f));
			ImGui::EndTableItem();
		}
	}
	
	if (entity.anti_aliasing_settings) {
		auto& settings = *entity.anti_aliasing_settings;
		ImGui::TableCombo("Anti Aliasing Method", (s32*)&settings.method, "None\0DLSS\0XeSS\0", (s32)AntiAliasingMethod::Count);
	}
	
	if (entity.exposure_settings) {
		auto& settings = *entity.exposure_settings;
		ImGui::TableCombo("Exposure Method", (s32*)&settings.method, "Manual\0Automatic\0", (s32)ExposureMethod::Count);
		
		if (settings.method == ExposureMethod::Manual) {
			ImGui::TableSliderFloat("Exposure Offset (EV)", &settings.manual_exposure_offset_ev, -8.f, 8.f);
		} else if (settings.method == ExposureMethod::Automatic) {
			ImGui::TableSliderFloat("Exposure Offset (EV)", &settings.automatic_exposure_offset_ev, -8.f, 8.f);
			
			ImGui::TableSliderFloat("Exposure Min (EV)", &settings.exposure_min_ev, -16.f, 16.f);
			ImGui::TableSliderFloat("Exposure Max (EV)", &settings.exposure_max_ev, -16.f, 16.f);
			ImGui::TableSliderFloat("Exposure Increase Half Time (s)", &settings.exposure_increase_half_time, 0.f, 8.f);
			ImGui::TableSliderFloat("Exposure Decrease Half Time (s)", &settings.exposure_decrease_half_time, 0.f, 8.f);
			
			ImGui::TableSliderFloat("Histogram Min Cutoff", &settings.histogram_min_cutoff, 0.f, 1.f);
			ImGui::TableSliderFloat("Histogram Max Cutoff", &settings.histogram_max_cutoff, 0.f, 1.f);
			ImGui::TableSliderFloat("Histogram Min (EV)", &settings.histogram_min_ev, -32.f, +32.f);
			ImGui::TableSliderFloat("Histogram Max (EV)", &settings.histogram_max_ev, -32.f, +32.f);
		}
	}
	
	if (entity.tone_mapping_settings) {
		auto& settings = *entity.tone_mapping_settings;
		
		ImGui::TableCombo("Tone Mapping Method", (s32*)&settings.method, "None\0GT7 HDR\0GT7 SDR\0Reinhard SDR\0", (s32)ToneMappingMethod::Count);
		
		if (settings.method == ToneMappingMethod::GT7_HDR) {
			ImGui::TableSliderFloat("Target Luminance (cd/m^2)", &settings.physical_target_luminance_hdr, 80.f, 4000.f);
		}
		
		if (settings.method == ToneMappingMethod::GT7_SDR) {
			ImGui::TableSliderFloat("Target Luminance (cd/m^2)", &settings.physical_target_luminance_sdr, 80.f, 500.f);
		}
		
		if (settings.method == ToneMappingMethod::GT7_HDR || settings.method == ToneMappingMethod::GT7_SDR) {
			ImGui::TableSliderFloat("Alpha", &settings.alpha, 0.f, 1.f);
			ImGui::TableSliderFloat("Mid Point", &settings.mid_point, 0.1f, 0.9f);
			ImGui::TableSliderFloat("Linear Section", &settings.linear_section, 0.25f, 0.75f);
			ImGui::TableSliderFloat("Toe Power", &settings.toe_power, 0.5f, 1.5f);
			
			ImGui::TableSliderFloat("Blend Ratio", &settings.blend_ratio, 0.f, 1.f);
			ImGui::TableSliderFloat("Fade Start", &settings.fade_start, 0.f, 2.f);
			ImGui::TableSliderFloat("Fade End", &settings.fade_end, 0.f, 2.f);
		}
	}
}

static bool AssetComponentEntityView(StackAllocator* alloc, AssetEntitySystem& asset_system, AssetEntityEditorQuery entity) {
	bool should_recreate_asset = false;
	
	if (entity.mesh_source_data) {
		ImGui::TableInputText("Mesh Source Data", entity.mesh_source_data->filepath, &asset_system.heap);
	}
	
	if (entity.mesh_runtime_data_layout) {
		auto guid_string = StringFormat(alloc, "0x%"_sl, (void*)entity.mesh_runtime_data_layout->file_guid);
		ImGui::TableInputText("File GUID", guid_string, nullptr);
		
		if (ImGui::BeginTableItem("File Version")) {
			ImGui::Text("%llu", entity.mesh_runtime_data_layout->version);
			ImGui::EndTableItem();
		}
		
		if (ImGui::BeginTableItem("Page Count")) {
			ImGui::Text("%llu", entity.mesh_runtime_data_layout->page_count);
			ImGui::EndTableItem();
		}
		
		if (ImGui::BeginTableItem("Meshlet Group Count")) {
			ImGui::Text("%llu", entity.mesh_runtime_data_layout->meshlet_group_count);
			ImGui::EndTableItem();
		}
		
		if (ImGui::BeginTableItem("Meshlet Count")) {
			ImGui::Text("%llu", entity.mesh_runtime_data_layout->meshlet_count);
			ImGui::EndTableItem();
		}
	}
	
	if (entity.mesh_runtime_data_layout) {
		if (ImGui::BeginTableItem("Reload From Source")) {
			if (ImGui::Button("Reload", ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
				should_recreate_asset = true;
				entity.mesh_runtime_data_layout->version = 0;
			}
			ImGui::EndTableItem();
		}
	}
	
	
	if (entity.texture_source_data) {
		ImGui::TableInputText("Texture Source Data", entity.texture_source_data->filepath, &asset_system.heap);
		
		compile_const char* texture_asset_target_encoding_names[(u32)TextureAssetTargetEncoding::Count] = {
			"BC1_UNORM_SRGB",
			"BC1_UNORM",
			"BC4_UNORM",
			"BC5_UNORM",
			"BC5_NORMAL_MAP",
		};
		
		if (ImGui::BeginTableItem("Encoding Format")) {
			ImGui::Combo("", (s32*)&entity.texture_source_data->target_encoding, texture_asset_target_encoding_names, (s32)TextureAssetTargetEncoding::Count);
			ImGui::EndTableItem();
		}
	}
	
	if (entity.texture_runtime_data_layout) {
		auto guid_string = StringFormat(alloc, "0x%"_sl, (void*)entity.texture_runtime_data_layout->file_guid);
		ImGui::TableInputText("File GUID", guid_string, nullptr);
		
		if (ImGui::BeginTableItem("File Version")) {
			ImGui::Text("%llu", entity.texture_runtime_data_layout->version);
			ImGui::EndTableItem();
		}
		
		auto size = entity.texture_runtime_data_layout->size;
		if (ImGui::BeginTableItem("Texture Size")) {
			if (size.type == TextureSize::Type::Texture3D) {
				ImGui::Text("%ux%ux%u, %u, %u", size.x, size.y, size.DepthSliceCount());
			} else {
				ImGui::Text("%ux%u", size.x, size.y, size.DepthSliceCount());
			}
			ImGui::EndTableItem();
		}
		
		if (ImGui::BeginTableItem("Texture Array Size")) {
			ImGui::Text("%u", size.ArraySliceCount());
			ImGui::EndTableItem();
		}
		
		if (ImGui::BeginTableItem("Texture Mip Count")) {
			ImGui::Text("%u", size.mips);
			ImGui::EndTableItem();
		}
	}
	
	if (entity.texture_descriptor_allocation) {
		auto descriptor_index_string = StringFormat(alloc, "%"_sl, entity.texture_descriptor_allocation->index);
		ImGui::TableInputText("Descriptor Index", descriptor_index_string, nullptr);
		
		if (ImGui::BeginTableItem("Preview")) {
			ImGui::ImageButtonEx("Texture", entity.texture_descriptor_allocation->index, ImVec2(128.f, 128.f));
			ImGui::EndTableItem();
		}
	}
	
	if (entity.texture_runtime_data_layout) {
		if (ImGui::BeginTableItem("Reload From Source")) {
			if (ImGui::Button("Reload", ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
				should_recreate_asset = true;
				entity.texture_runtime_data_layout->version = 0;
			}
			ImGui::EndTableItem();
		}
	}
	
	if (entity.material_texture_data) {
		auto* texture_data = entity.material_texture_data;
		
		float3 albedo = Math::EncodeSRGB(Math::DecodeR10G10B10(texture_data->default_albedo)); // Edit in SRGB, store in linear.
		if (ImGui::TableEntityComboBoxWithColor(alloc, "Albedo", &asset_system, &albedo.x, 3, &texture_data->albedo.guid, ECS::GetEntityTypeID<TextureAssetType>::id)) {
			texture_data->default_albedo = Math::EncodeR10G10B10(Math::DecodeSRGB(albedo));
		}
		
		ImGui::TableEntityComboBox(alloc, "Normal", &asset_system, &texture_data->normal.guid, ECS::GetEntityTypeID<TextureAssetType>::id);
		
		float roughness = Math::DecodeR16_FLOAT(texture_data->default_roughness);
		if (ImGui::TableEntityComboBoxWithColor(alloc, "Roughness", &asset_system, &roughness, 1, &texture_data->roughness.guid, ECS::GetEntityTypeID<TextureAssetType>::id)) {
			texture_data->default_roughness = Math::EncodeR16_FLOAT(roughness);
		}
		
		float metalness = Math::DecodeR16_FLOAT(texture_data->default_metalness);
		if (ImGui::TableEntityComboBoxWithColor(alloc, "Metalness", &asset_system, &metalness, 1, &texture_data->metalness.guid, ECS::GetEntityTypeID<TextureAssetType>::id)) {
			texture_data->default_metalness = Math::EncodeR16_FLOAT(metalness);
		}
	}
	
	if (entity.material_asset) {
		ImGui::TableEntityComboBox(alloc, "Material Asset", &asset_system, &entity.material_asset->guid, ECS::GetEntityTypeID<MaterialAssetType>::id);
	}
	
	
	if (entity.world_source_data) {
		auto source_data_filename = StringFormat(alloc, "./Assets/%x..csb"_sl, entity.world_source_data->world_entity_guid);
		ImGui::TableInputText("World Source Data", source_data_filename, nullptr);
	}
	
	return should_recreate_asset;
}


static bool ComponentEntityView(StackAllocator* alloc, String undo_label, WorldEntitySystem* world_system, AssetEntitySystem& asset_system, UndoRedoSystem& undo_redo_system, u64 entity_guid) {
	auto& entity_system = world_system != nullptr ? (EntitySystemBase&)*world_system : (EntitySystemBase&)asset_system;
	
	auto typed_entity_id = FindEntityByGUID(entity_system, entity_guid);
	auto* array = &entity_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
	bool should_recreate_asset = false;
	
	BeginUndoRedoCommand(undo_label, undo_redo_system, entity_system, entity_guid);
	
	auto shared_entity_components = ExtractComponentStreams<SharedEntityEditorQuery>(array, typed_entity_id.entity_id);
	SharedComponentEntityView(alloc, entity_system, shared_entity_components);
	
	if (world_system != nullptr) {
		auto world_entity_components = ExtractComponentStreams<WorldEntityEditorQuery>(array, typed_entity_id.entity_id);
		WorldComponentEntityView(alloc, *world_system, asset_system, world_entity_components);
	} else {
		auto asset_entity_components = ExtractComponentStreams<AssetEntityEditorQuery>(array, typed_entity_id.entity_id);
		should_recreate_asset = AssetComponentEntityView(alloc, asset_system, asset_entity_components);
	}
	
	bool is_dragging = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	bool is_dirty = EndUndoRedoCommand(undo_redo_system, is_dragging);
	
	if (is_dirty) {
		BitArraySetBit(array->dirty_mask, typed_entity_id.entity_id.index);
	}
	
	if (should_recreate_asset) {
		BitArraySetBit(array->created_mask, typed_entity_id.entity_id.index);
	}
	
	return is_dirty;
}

static bool SingleEntityView(String label, StackAllocator* alloc, WorldEntitySystem* world_system, AssetEntitySystem& asset_system, UndoRedoSystem& undo_redo_system, u64 entity_guid, bool is_disabled = false) {
	ImGui::Begin(label.data, nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
	defer{ ImGui::End(); };
	
	auto table_flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY;
	
	if (ImGui::BeginTable("Components", 2, table_flags) == false) return false;
	defer{ ImGui::EndTable(); };
	
	ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.f);
	ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch, 3.f);
	ImGui::TableHeadersRow();
	
	if (entity_guid == 0) return false;
	
	// Items should span full width of the column.
	ImGui::PushItemWidth(-FLT_MIN);
	defer{ ImGui::PopItemWidth(); };
	
	ImGuiScopeID((void*)entity_guid);
	
	ImGui::BeginDisabled(is_disabled);
	defer{ ImGui::EndDisabled(); };
	
	return ComponentEntityView(alloc, label, world_system, asset_system, undo_redo_system, entity_guid);
}

static bool MultiEntityView(String label, StackAllocator* alloc, WorldEntitySystem* world_system, AssetEntitySystem& asset_system, UndoRedoSystem& undo_redo_system, EditorSelectionStateEntity selection_state_entity) {
	auto& selected_entities_hash_table = selection_state_entity.selection_state->selected_entities_hash_table;
	
	u64  entity_guid = selected_entities_hash_table.count != 0 ? (*selected_entities_hash_table.begin()).key : 0;
	bool is_disabled = selected_entities_hash_table.count >= 2;
	
	return SingleEntityView(label, alloc, world_system, asset_system, undo_redo_system, entity_guid, is_disabled);
}

bool EditorPropertiesWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, EditorSelectionStateEntity asset_selection_state_entity, u64 world_entity_guid) {
	bool result = false;
	result |= MultiEntityView("Asset Editor"_sl, alloc, nullptr, asset_system, undo_redo_system, asset_selection_state_entity);
	result |= MultiEntityView("Entity Editor"_sl, alloc, &world_system, asset_system, undo_redo_system, world_selection_state_entity);
	result |= SingleEntityView("World Entity"_sl, alloc, &world_system, asset_system, undo_redo_system, world_entity_guid);
	return result;
}
