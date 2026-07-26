#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"

struct AssetEntitySystem;
struct EditorIconCache;
struct EditorSelectionStateEntity;
struct GraphicsContext;
struct LevelEditor;
struct UndoRedoSystem;
struct VirtualResourceTable;
struct WorldEntitySystem;

struct LevelEditorIO {
	u64 world_asset_guid_to_load  = 0;
	u64 camera_entity_guid_to_set = 0;
	
	EditorIconCache* icon_cache = nullptr;
	LevelEditor* level_editor = nullptr;
};

struct EditorWorldView {
	VirtualResourceTable* resource_table = nullptr;
	WorldEntitySystem* world_system = nullptr;
	u64 world_entity_guid = 0;
};

void LevelEditorUpdate(StackAllocator* alloc, GraphicsContext* graphics_context, UndoRedoSystem& undo_redo_system, AssetEntitySystem& asset_system, LevelEditorIO& level_editor_io, Array<EditorWorldView>& editor_world_views);

void EditorUndoRedoHistoryWindow(UndoRedoSystem& undo_redo_system);

void EditorShaderStatisticsWindow(StackAllocator* alloc, GraphicsContext* graphics_context);

bool EditorPropertiesWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, EditorSelectionStateEntity asset_selection_state_entity, u64 world_entity_guid);

void EditorOutlinerWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, EditorSelectionStateEntity selection_state_entity, LevelEditorIO& level_editor_io);

void EditorAssetBrowserWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity selection_state_entity, LevelEditorIO& level_editor_io);

void EditorViewportWindow(StackAllocator* alloc, UndoRedoSystem& undo_redo_system, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, EditorSelectionStateEntity world_selection_state_entity, u64 world_entity_guid, GraphicsContext* graphics_context, VirtualResourceTable* resource_table, Array<EditorWorldView>& editor_world_views);


LevelEditor* CreateLevelEditor(StackAllocator* alloc, GraphicsContext* graphics_context, AssetEntitySystem& asset_system);
void ReleaseLevelEditor(LevelEditor* level_editor, GraphicsContext* graphics_context);

EditorIconCache* CreateEditorIconCache(StackAllocator* alloc, GraphicsContext* graphics_context);
void ReleaseEditorIconCache(EditorIconCache* icon_cache, GraphicsContext* graphics_context);


void EditorIconCacheUpdate(StackAllocator* alloc, EditorIconCache* icon_cache, GraphicsContext* graphics_context, AssetEntitySystem& asset_system, Array<EditorWorldView>& editor_world_views);
void EditorIconCacheDrawMeshIcon(EditorIconCache* icon_cache, u64 mesh_asset_guid);

