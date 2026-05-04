#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicSaveLoad.h"
#include "EntitySystem/EntitySystem.h"

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
	
	UndoRedoCommand cross_frame_command;
	UndoRedoCommand pending_command;
	u64 pending_command_id     = 0;
	u64 cross_frame_command_id = 0;
	
	// Zero group index means no group.
	u64 group_index_allocator = 1;
	u64 group_index = 0;
};

void InitializeUndoRedoSystem(UndoRedoSystem& system, HeapAllocator* heap);
void ReleaseUndoRedoSystem(UndoRedoSystem& system);
void ResetUndoRedoSystem(UndoRedoSystem& system);

void BeginUndoRedoGroup(UndoRedoSystem& system);
void EndUndoRedoGroup(UndoRedoSystem& system);
void BeginUndoRedoCommand(String label, UndoRedoSystem& system, EntitySystemBase& entity_system, u64 entity_guid);
bool EndUndoRedoCommand(UndoRedoSystem& system, EntitySystemBase& entity_system, bool is_dragging = false);
void UndoRedoRemoveEntity(UndoRedoSystem& system, EntitySystemBase& entity_system, u64 entity_guid);
void UndoRedoCreateEntity(UndoRedoSystem& system, EntitySystemBase& entity_system, u64 entity_guid);

bool ExecuteUndo(UndoRedoSystem& system, EntitySystemBase& entity_system);
bool ExecuteRedo(UndoRedoSystem& system, EntitySystemBase& entity_system);
