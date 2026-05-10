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
	DebugAssert(system.scope_begin_command_id == 0, "ResetUndoRedoSystem cannot be called while recording undo redo command.");
	
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
	command.entity_guid   = entity_guid;
	command.group_index   = group_index;
	command.command_type  = command_type;
	command.entity_system = &entity_system;
	
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

static void ExecuteUndoRedoCommand(UndoRedoBuffer& undo_redo_buffer, UndoRedoCommand command) {
	auto& entity_system = *command.entity_system;
	
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
	DebugAssert(system.scope_begin_command_id == 0, "BeginUndoRedoCommand/EndUndoRedoCommand mismatch.");
	
	u64 id = ComputeHash(label); // IDs are used to distinguish between different Begin/End scopes that edit the same entity.
	system.scope_begin_command_id = id;
	
	system.scope_begin_command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::SaveLoad, system.group_index);
	
	if ((system.cross_frame_command.entity_guid == entity_guid) && (system.cross_frame_command_id == id)) { // We already have a command stashed away.
		system.initial_state_command = system.cross_frame_command;
		system.cross_frame_command = {};
	} else {
		system.initial_state_command = system.scope_begin_command;
	}
}

static bool IsSaveLoadCommandDifferent(const u8* data, const UndoRedoCommand& lh, const UndoRedoCommand& rh) {
	return (lh.size != rh.size) || (lh.size != 0 && memcmp(data + lh.offset, data + rh.offset, lh.size) != 0);
}

bool EndUndoRedoCommand(UndoRedoSystem& system, bool is_dragging) {
	DebugAssert(system.scope_begin_command_id != 0, "BeginUndoRedoCommand/EndUndoRedoCommand mismatch.");
	
	auto& entity_system = *system.scope_begin_command.entity_system;
	auto scope_end_command = CreateUndoRedoCommand(system.undo_buffer, entity_system, system.initial_state_command.entity_guid, UndoRedoCommandType::SaveLoad, system.group_index);
	
	u8* save_load_buffer_data = system.undo_buffer.save_load_buffer.data.data;
	bool entity_changed = IsSaveLoadCommandDifferent(save_load_buffer_data, system.initial_state_command, scope_end_command);
	
	// When scope_begin_command and initial_state_command are the same, we don't need to call IsSaveLoadCommandDifferent twice.
	bool entity_changed_this_frame = entity_changed;
	if (system.scope_begin_command.offset != system.initial_state_command.offset) {
		entity_changed_this_frame = IsSaveLoadCommandDifferent(save_load_buffer_data, system.scope_begin_command, scope_end_command);
	}
	
	// Pop scope_end_command data (and scope_begin_command data if it's not the same as the initial_state_command data), it's not needed anymore.
	system.undo_buffer.save_load_buffer.data.count = system.initial_state_command.offset + system.initial_state_command.size;
	
	if (is_dragging) { // Keep the pending command on the stack across frames.
		FlushCrossFrameUndoCommand(system);
		system.cross_frame_command    = system.initial_state_command;
		system.cross_frame_command_id = system.scope_begin_command_id;
	} else if (entity_changed) { // Entity changed and we're not dragging, flush the whole undo stack.
		FlushCrossFrameUndoCommand(system);
		AppendUndoCommand(system, system.initial_state_command);
	} else { // We're not dragging and the entity hasn't changed, pop the pending command off the stack.
		system.undo_buffer.save_load_buffer.data.count = system.initial_state_command.offset;
	}
	
	system.scope_begin_command    = {};
	system.initial_state_command  = {};
	system.scope_begin_command_id = 0;
	
	return entity_changed_this_frame;
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

static bool ExecuteUndoRedoGroup(UndoRedoSystem& system, UndoRedoBuffer& src_buffer, UndoRedoBuffer& dst_buffer) {
	if (src_buffer.commands.count == 0) return false;
	
	u64 group_index = ArrayLastElement(src_buffer.commands).group_index;
	do {
		auto src_command = ArrayPopLast(src_buffer.commands);
		auto dst_command = CreateUndoRedoCommand(dst_buffer, *src_command.entity_system, src_command.entity_guid, ReverseUndoRedoCommandType(src_command.command_type), src_command.group_index);
		
		ArrayAppend(dst_buffer.commands, system.heap, dst_command);
		ExecuteUndoRedoCommand(src_buffer, src_command);
	} while (group_index && src_buffer.commands.count && ArrayLastElement(src_buffer.commands).group_index == group_index);
	
	return true;
}

bool ExecuteUndo(UndoRedoSystem& system) {
	if (system.cross_frame_command.entity_guid != 0) return false; // Can't modify the undo_buffer while we have a cross_frame_command on it.
	return ExecuteUndoRedoGroup(system, system.undo_buffer, system.redo_buffer);
}

bool ExecuteRedo(UndoRedoSystem& system) {
	if (system.cross_frame_command.entity_guid != 0) return false; // Can't modify the undo_buffer while we have a cross_frame_command on it.
	return ExecuteUndoRedoGroup(system, system.redo_buffer, system.undo_buffer);
}
