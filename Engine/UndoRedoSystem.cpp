#include "Basic/BasicBitArray.h"
#include "UndoRedoSystem.h"

void InitializeUndoRedoSystem(UndoRedoSystem& system, HeapAllocator* heap) {
	system.heap = heap;
	system.undo_buffer.alloc = CreateStackAllocator(64 * 1024 * 1024, 64 * 1024);
	system.redo_buffer.alloc = CreateStackAllocator(64 * 1024 * 1024, 64 * 1024);
	system.undo_buffer.save_load_buffer.alloc = &system.undo_buffer.alloc;
	system.redo_buffer.save_load_buffer.alloc = &system.redo_buffer.alloc;
}

void ReleaseUndoRedoSystem(UndoRedoSystem& system) {
	ReleaseStackAllocator(system.undo_buffer.alloc);
	ReleaseStackAllocator(system.redo_buffer.alloc);
}

void ResetUndoRedoSystem(UndoRedoSystem& system) {
	DebugAssert(system.pending_command_id == 0, "ResetUndoRedoSystem cannot be called while recording undo redo command.");
	
	system.undo_buffer.commands.count = 0;
	system.undo_buffer.save_load_buffer.data.count = 0;
	
	system.redo_buffer.commands.count = 0;
	system.redo_buffer.save_load_buffer.data.count = 0;
	
	system.cross_frame_command = {};
	system.cross_frame_command_id = 0;
}

static void AppendUndoCommand(UndoRedoSystem& system, UndoRedoCommand& command) {
	system.redo_buffer.commands.count = 0;
	system.redo_buffer.save_load_buffer.data.count = 0;
	
	ArrayAppend(system.undo_buffer.commands, system.heap, command);
}

static void FlushCrossFrameUndoCommand(UndoRedoSystem& system) {
	if (system.cross_frame_command.entity_guid == 0) return;
	
	AppendUndoCommand(system, system.cross_frame_command);
	system.cross_frame_command = {};
	system.cross_frame_command_id = 0;
}

static UndoRedoCommand CreateUndoRedoCommand(UndoRedoBuffer& undo_redo_buffer, EntitySystemBase& entity_system, u64 entity_guid, UndoRedoCommandType command_type, u64 group_index) {
	UndoRedoCommand command;
	command.entity_guid  = entity_guid;
	command.group_index  = group_index;
	command.command_type = command_type;
	
	if (command_type == UndoRedoCommandType::SaveLoad || command_type == UndoRedoCommandType::CreateEntity) {
		auto typed_entity_id = FindEntityByGUID(entity_system, entity_guid);
		auto* entity_array = &entity_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
		
		undo_redo_buffer.save_load_buffer.is_saving  = true;
		undo_redo_buffer.save_load_buffer.is_loading = false;
		
		command.entity_type_id = typed_entity_id.entity_type_id;
		command.offset = undo_redo_buffer.save_load_buffer.data.count;
		SaveLoadEntityForTooling(undo_redo_buffer.save_load_buffer, entity_array, typed_entity_id.entity_id);
		command.size = undo_redo_buffer.save_load_buffer.data.count - command.offset;
	} else if (command_type == UndoRedoCommandType::RemoveEntity) {
		// No need to do anything.
	}
	
	return command;
}

static void ExecuteUndoRedoCommand(UndoRedoBuffer& undo_redo_buffer, EntitySystemBase& entity_system, UndoRedoCommand command) {
	if (command.command_type == UndoRedoCommandType::CreateEntity) {
		CreateEntity(entity_system, command.entity_type_id, command.entity_guid);
	}
	
	if (command.command_type == UndoRedoCommandType::SaveLoad || command.command_type == UndoRedoCommandType::CreateEntity) {
		auto typed_entity_id = FindEntityByGUID(entity_system, command.entity_guid);
		auto* entity_array = &entity_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
		
		undo_redo_buffer.save_load_buffer.heap       = &entity_system.heap;
		undo_redo_buffer.save_load_buffer.data.count = command.offset;
		undo_redo_buffer.save_load_buffer.is_saving  = false;
		undo_redo_buffer.save_load_buffer.is_loading = true;
		
		SaveLoadEntityForTooling(undo_redo_buffer.save_load_buffer, entity_array, typed_entity_id.entity_id);
		u64 size = undo_redo_buffer.save_load_buffer.data.count - command.offset;
		DebugAssert(size == command.size, "Mismatch between command sizes. (%/%).", size, command.size);
		
		undo_redo_buffer.save_load_buffer.data.count = command.offset;
		
		BitArraySetBit(entity_array->dirty_mask, typed_entity_id.entity_id.index);
	} else if (command.command_type == UndoRedoCommandType::RemoveEntity) {
		RemoveEntityByGUID(entity_system, command.entity_guid);
	}
}

void BeginUndoRedoGroup(UndoRedoSystem& system) {
	DebugAssert(system.group_index == 0, "Nested Undo/Redo groups are not supported. Already in group %..", system.group_index);
	system.group_index = system.group_index_allocator;
	system.group_index_allocator += 1;
}

void EndUndoRedoGroup(UndoRedoSystem& system) {
	system.group_index = 0;
}

void BeginUndoRedoCommand(String label, UndoRedoSystem& system, EntitySystemBase& entity_system, u64 entity_guid) {
	DebugAssert(system.pending_command.entity_guid == 0, "BeginUndoRedoCommand/EndUndoRedoCommand mismatch.");
	
	u64 id = ComputeHash(label); // IDs are used to distinguish between different Begin/End scopes that could edit the same entity.
	system.pending_command_id = id;
	
	if ((system.cross_frame_command.entity_guid == entity_guid) && (system.cross_frame_command_id == id)) { // We already have a command stashed away.
		system.pending_command = system.cross_frame_command;
		system.cross_frame_command = {};
	} else {
		system.pending_command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::SaveLoad, system.group_index);
	}
}

bool EndUndoRedoCommand(UndoRedoSystem& system, EntitySystemBase& entity_system, bool is_dragging) {
	DebugAssert(system.pending_command.entity_guid != 0, "BeginUndoRedoCommand/EndUndoRedoCommand mismatch.");
	
	auto old_command = system.pending_command;
	auto new_command = CreateUndoRedoCommand(system.undo_buffer, entity_system, old_command.entity_guid, UndoRedoCommandType::SaveLoad, system.group_index);
	
	u8* data = system.undo_buffer.save_load_buffer.data.data;
	bool entity_changed = (old_command.size != new_command.size) || (new_command.size != 0 && memcmp(data + old_command.offset, data + new_command.offset, new_command.size) != 0);
	
	// Pop command data, it's never going to be used again.
	system.undo_buffer.save_load_buffer.data.count = old_command.offset + old_command.size;
	
	if (is_dragging) { // Keep the pending command on the stack across frames.
		FlushCrossFrameUndoCommand(system);
		system.cross_frame_command = old_command;
		system.cross_frame_command_id = system.pending_command_id;
	} else if (entity_changed) { // Entity changed and we're not dragging, flush the whole undo stack.
		FlushCrossFrameUndoCommand(system);
		AppendUndoCommand(system, old_command);
	} else { // We're not dragging and the entity haven't changed. Pop the pending command off the stack.
		system.undo_buffer.save_load_buffer.data.count = old_command.offset;
	}
	
	system.pending_command = {};
	system.pending_command_id = 0;
	
	// TODO: Detect the case when we drag from some state to the old state and report it as entity_changed.
	// For now always report that entity changes during dragging.
	return entity_changed || is_dragging;
}

void UndoRedoRemoveEntity(UndoRedoSystem& system, EntitySystemBase& entity_system, u64 entity_guid) {
	FlushCrossFrameUndoCommand(system);
	auto command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::CreateEntity, system.group_index);
	AppendUndoCommand(system, command);
}

void UndoRedoCreateEntity(UndoRedoSystem& system, EntitySystemBase& entity_system, u64 entity_guid) {
	FlushCrossFrameUndoCommand(system);
	auto command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::RemoveEntity, system.group_index);
	AppendUndoCommand(system, command);
}


static UndoRedoCommandType ReverseUndoRedoCommandType(UndoRedoCommandType command_type) {
	switch (command_type) {
	case UndoRedoCommandType::SaveLoad:     return UndoRedoCommandType::SaveLoad;
	case UndoRedoCommandType::CreateEntity: return UndoRedoCommandType::RemoveEntity;
	case UndoRedoCommandType::RemoveEntity: return UndoRedoCommandType::CreateEntity;
	default: DebugAssertAlways("Unhandled UndoRedoCommandType '%'.", (u32)command_type); return UndoRedoCommandType::SaveLoad;
	}
}

static void ExecuteUndoRedoGroup(UndoRedoSystem& system, EntitySystemBase& entity_system, UndoRedoBuffer& src_buffer, UndoRedoBuffer& dst_buffer) {
	if (src_buffer.commands.count == 0) return;
	
	u64 group_index = ArrayLastElement(src_buffer.commands).group_index;
	do {
		auto src_command = ArrayPopLast(src_buffer.commands);
		auto dst_command = CreateUndoRedoCommand(dst_buffer, entity_system, src_command.entity_guid, ReverseUndoRedoCommandType(src_command.command_type), src_command.group_index);
		
		ArrayAppend(dst_buffer.commands, system.heap, dst_command);
		ExecuteUndoRedoCommand(src_buffer, entity_system, src_command);
	} while (group_index && src_buffer.commands.count && ArrayLastElement(src_buffer.commands).group_index == group_index);
}

void ExecuteUndo(UndoRedoSystem& system, EntitySystemBase& entity_system) {
	if (system.cross_frame_command.entity_guid != 0) return; // Can't modify the undo_buffer while we have a cross_frame_command on it.
	ExecuteUndoRedoGroup(system, entity_system, system.undo_buffer, system.redo_buffer);
}

void ExecuteRedo(UndoRedoSystem& system, EntitySystemBase& entity_system) {
	if (system.cross_frame_command.entity_guid != 0) return; // Can't modify the undo_buffer while we have a cross_frame_command on it.
	ExecuteUndoRedoGroup(system, entity_system, system.redo_buffer, system.undo_buffer);
}
