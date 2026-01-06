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

static void AppendUndoCommand(UndoRedoSystem& system, UndoRedoCommand command) {
	system.redo_buffer.commands.count = 0;
	system.redo_buffer.save_load_buffer.data.count = 0;
	
	ArrayAppend(system.undo_buffer.commands, system.heap, command);
}

static UndoRedoCommand CreateUndoRedoCommand(UndoRedoBuffer& undo_redo_buffer, EntitySystem& entity_system, u64 entity_guid, UndoRedoCommandType command_type, u64 group_index) {
	UndoRedoCommand command;
	command.entity_guid  = entity_guid;
	command.group_index  = group_index;
	command.command_type = command_type;
	
	if (command_type == UndoRedoCommandType::SaveLoad || command_type == UndoRedoCommandType::CreateEntity) {
		auto* element = HashTableFind(entity_system.entity_guid_to_entity_id, entity_guid);
		DebugAssert(element, "Failed to find entity by GUID 0x%x.", entity_guid);
		
		auto typed_entity_id = element->value;
		
		auto* entity_array = &entity_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
		u32 stream_offset = entity_array->entity_id_to_stream_index[typed_entity_id.entity_id.index];
		
		undo_redo_buffer.save_load_buffer.is_saving  = true;
		undo_redo_buffer.save_load_buffer.is_loading = false;
		
		command.entity_type_id = typed_entity_id.entity_type_id;
		command.offset = undo_redo_buffer.save_load_buffer.data.count;
		SaveLoadEntityForTooling(undo_redo_buffer.save_load_buffer, entity_array, stream_offset);
		command.size = undo_redo_buffer.save_load_buffer.data.count - command.offset;
	} else if (command_type == UndoRedoCommandType::RemoveEntity) {
		// No need to do anything.
	}
	
	return command;
}

static void ExecuteUndoRedoCommand(UndoRedoBuffer& undo_redo_buffer, EntitySystem& entity_system, UndoRedoCommand command) {
	if (command.command_type == UndoRedoCommandType::CreateEntity) {
		CreateEntities(entity_system, command.entity_type_id, 1, { &command.entity_guid, 1 });
	}
	
	if (command.command_type == UndoRedoCommandType::SaveLoad || command.command_type == UndoRedoCommandType::CreateEntity) {
		auto* element = HashTableFind(entity_system.entity_guid_to_entity_id, command.entity_guid);
		DebugAssert(element, "Failed to find entity by GUID 0x%x.", command.entity_guid);
		
		auto typed_entity_id = element->value;
		
		auto* entity_array = &entity_system.entity_type_arrays[typed_entity_id.entity_type_id.index];
		u32 stream_offset = entity_array->entity_id_to_stream_index[typed_entity_id.entity_id.index];
		
		undo_redo_buffer.save_load_buffer.heap       = &entity_system.heap;
		undo_redo_buffer.save_load_buffer.data.count = command.offset;
		undo_redo_buffer.save_load_buffer.is_saving  = false;
		undo_redo_buffer.save_load_buffer.is_loading = true;
		
		SaveLoadEntityForTooling(undo_redo_buffer.save_load_buffer, entity_array, stream_offset);
		u64 size = undo_redo_buffer.save_load_buffer.data.count - command.offset;
		DebugAssert(size == command.size, "Mismatch between command sizes. (%/%).", size, command.size);
		
		undo_redo_buffer.save_load_buffer.data.count = command.offset;
		
		BitArraySetBit(entity_array->dirty_mask, stream_offset);
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

void BeginUndoRedoCommand(UndoRedoSystem& system, EntitySystem& entity_system, u64 entity_guid) {
	DebugAssert(system.pending_command.entity_guid == 0 || system.pending_command.entity_guid == entity_guid, "Unfinished pending command 0x%x..", system.pending_command.entity_guid);
	
	if (system.pending_command.entity_guid == 0) {
		system.pending_command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::SaveLoad, system.group_index);
	}
}

bool EndUndoRedoCommand(UndoRedoSystem& system, EntitySystem& entity_system, bool is_dragging) {
	if (system.pending_command.entity_guid == 0) return false;
	
	auto command = CreateUndoRedoCommand(system.undo_buffer, entity_system, system.pending_command.entity_guid, UndoRedoCommandType::SaveLoad, system.group_index);
	
	u8* data = system.undo_buffer.save_load_buffer.data.data;
	bool entity_changed = (system.pending_command.size != command.size) || (command.size != 0 && memcmp(data + system.pending_command.offset, data + command.offset, command.size) != 0);
	
	if (entity_changed || is_dragging) {
		system.undo_buffer.save_load_buffer.data.count = system.pending_command.offset + system.pending_command.size;
		if (is_dragging == false) AppendUndoCommand(system, system.pending_command);
	} else {
		system.undo_buffer.save_load_buffer.data.count = system.pending_command.offset;
	}
	
	if (is_dragging == false) {
		system.pending_command = {};
	}
	
	return entity_changed;
}

void UndoRedoRemoveEntity(UndoRedoSystem& system, EntitySystem& entity_system, u64 entity_guid) {
	DebugAssert(system.pending_command.entity_guid == 0, "Unfinished pending command 0x%x..", system.pending_command.entity_guid);
	
	auto command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::CreateEntity, system.group_index);
	AppendUndoCommand(system, command);
}

void UndoRedoCreateEntity(UndoRedoSystem& system, EntitySystem& entity_system, u64 entity_guid) {
	DebugAssert(system.pending_command.entity_guid == 0, "Unfinished pending command 0x%x..", system.pending_command.entity_guid);
	
	auto command = CreateUndoRedoCommand(system.undo_buffer, entity_system, entity_guid, UndoRedoCommandType::RemoveEntity, system.group_index);
	AppendUndoCommand(system, command);
}


static UndoRedoCommandType ReverseUndoRedoCommandType(UndoRedoCommandType command_type) {
	switch (command_type) {
	default:
	case UndoRedoCommandType::SaveLoad:     return UndoRedoCommandType::SaveLoad;
	case UndoRedoCommandType::CreateEntity: return UndoRedoCommandType::RemoveEntity;
	case UndoRedoCommandType::RemoveEntity: return UndoRedoCommandType::CreateEntity;
	}
}

static void ExecuteUndoRedoGroup(UndoRedoSystem& system, EntitySystem& entity_system, UndoRedoBuffer& src_buffer, UndoRedoBuffer& dst_buffer) {
	if (src_buffer.commands.count == 0) return;
	
	u64 group_index = ArrayLastElement(src_buffer.commands).group_index;
	do {
		auto src_command = ArrayPopLast(src_buffer.commands);
		auto dst_command = CreateUndoRedoCommand(dst_buffer, entity_system, src_command.entity_guid, ReverseUndoRedoCommandType(src_command.command_type), src_command.group_index);
		
		ArrayAppend(dst_buffer.commands, system.heap, dst_command);
		ExecuteUndoRedoCommand(src_buffer, entity_system, src_command);
	} while (group_index && src_buffer.commands.count && ArrayLastElement(src_buffer.commands).group_index == group_index);
}

void ExecuteUndo(UndoRedoSystem& system, EntitySystem& entity_system) {
	ExecuteUndoRedoGroup(system, entity_system, system.undo_buffer, system.redo_buffer);
}

void ExecuteRedo(UndoRedoSystem& system, EntitySystem& entity_system) {
	ExecuteUndoRedoGroup(system, entity_system, system.redo_buffer, system.undo_buffer);
}
