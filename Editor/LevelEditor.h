#pragma once
#include "Basic/Basic.h"

struct AssetEntitySystem;
struct LevelEditorContext;
struct RecordContext;
struct WorldEntitySystem;

LevelEditorContext* CreateLevelEditorContext(StackAllocator* alloc, HeapAllocator* heap, WorldEntitySystem& world_system, AssetEntitySystem& asset_system);
void ReleaseLevelEditorContext(LevelEditorContext* editor_context);

void LevelEditorUpdate(LevelEditorContext* editor_context, StackAllocator* alloc, RecordContext* record_context, WorldEntitySystem& world_system, AssetEntitySystem& asset_system, u64 world_entity_guid);
