#pragma once
#include "Basic/Basic.h"

struct AssetEntitySystem;
struct EditorSelectionStateEntity;
struct GraphicsContext;
struct UndoRedoSystem;
struct WorldEntitySystem;

struct LevelEditorIO {
	u64 world_asset_guid_to_load  = 0;
	u64 camera_entity_guid_to_set = 0;
};

u64 LoadOrCreateDefaultEntitySystems(StackAllocator* alloc, WorldEntitySystem& world_system, AssetEntitySystem& asset_system);

void LevelEditorUpdate(StackAllocator* alloc, GraphicsContext* graphics_context, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, LevelEditorIO& level_editor_io, u64& world_entity_guid);

void EditorUndoRedoHistoryWindow(UndoRedoSystem& undo_redo_system);

void EditorShaderStatisticsWindow(StackAllocator* alloc, GraphicsContext* graphics_context);

bool EditorPropertiesWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, EditorSelectionStateEntity asset_selection_state_entity, u64 world_entity_guid);

void EditorOutlinerWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity, LevelEditorIO& level_editor_io);

void EditorAssetBrowserWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity selection_state_entity, LevelEditorIO& level_editor_io);

void EditorViewportWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, u64 world_entity_guid, GraphicsContext* graphics_context);
