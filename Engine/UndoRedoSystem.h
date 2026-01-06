#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicSaveLoad.h"
#include "EntitySystem.h"

enum struct UndoRedoCommandType : u32 {
	SaveLoad     = 0,
	CreateEntity = 1,
	RemoveEntity = 2,
};

struct UndoRedoCommand {
	u64 entity_guid = 0;
	u64 group_index = 0;
	
	u64 offset = 0;
	u64 size   = 0;
	
	EntityTypeID entity_type_id;
	UndoRedoCommandType command_type = UndoRedoCommandType::SaveLoad;
};

struct UndoRedoBuffer {
	StackAllocator alloc;
	SaveLoadBuffer save_load_buffer;
	Array<UndoRedoCommand> commands;
};

struct UndoRedoSystem {
	HeapAllocator* heap = nullptr;
	
	UndoRedoBuffer undo_buffer;
	UndoRedoBuffer redo_buffer;
	
	UndoRedoCommand pending_command;
	
	// Zero group index means no group.
	u64 group_index_allocator = 1;
	u64 group_index = 0;
};

void InitializeUndoRedoSystem(UndoRedoSystem& system, HeapAllocator* heap);
void ReleaseUndoRedoSystem(UndoRedoSystem& system);

void BeginUndoRedoGroup(UndoRedoSystem& system);
void EndUndoRedoGroup(UndoRedoSystem& system);
void BeginUndoRedoCommand(UndoRedoSystem& system, EntitySystem& entity_system, u64 entity_guid);
bool EndUndoRedoCommand(UndoRedoSystem& system, EntitySystem& entity_system, bool is_dragging = false);
void UndoRedoRemoveEntity(UndoRedoSystem& system, EntitySystem& entity_system, u64 entity_guid);
void UndoRedoCreateEntity(UndoRedoSystem& system, EntitySystem& entity_system, u64 entity_guid);

void ExecuteUndo(UndoRedoSystem& system, EntitySystem& entity_system);
void ExecuteRedo(UndoRedoSystem& system, EntitySystem& entity_system);
