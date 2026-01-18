#pragma once
#include "Basic/Basic.h"

struct LevelEditorContext;
struct EntitySystem;
struct RecordContext;

LevelEditorContext* CreateLevelEditorContext(StackAllocator* alloc, HeapAllocator* heap, EntitySystem& entity_system);
void ReleaseLevelEditorContext(LevelEditorContext* editor_context);

void LevelEditorUpdate(LevelEditorContext* editor_context, StackAllocator* alloc, RecordContext* record_context, EntitySystem& entity_system, u64 world_entity_guid);
