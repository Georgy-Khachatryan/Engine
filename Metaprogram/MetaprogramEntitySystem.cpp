#include "Basic/Basic.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "MetaprogramSystems.h"
#include "MetaprogramCommon.h"
#include "TypeInfo.h"
#include "Engine/EntitySystem.h"

struct ComponentTypeInfoKey {
	TypeInfoStruct* type_info = nullptr;
	ComponentType component_type = ComponentType::CPU;
	bool generate_save_load_callback = false;
	u32 name_hash = 0;
	
	bool operator== (const ComponentTypeInfoKey& other) const { return type_info == other.type_info && component_type == other.component_type; }
};

static u64 ComputeHash(const ComponentTypeInfoKey& key) {
	return ComputeHash64((u64)key.type_info, (u64)key.component_type);
}

struct EntityComponentMetadata {
	TypeInfoStruct* type_info = nullptr;
	ComponentType component_type = ComponentType::CPU;
	
	ComponentTypeID component_type_id;
	VirtualResourceID resource_id = (VirtualResourceID)0;
};

struct EntityTypeMetadata {
	TypeInfoStruct* type_info = nullptr;
	ArrayView<EntityComponentMetadata> components;
	u32 cpu_component_count = 0;
	u32 gpu_component_count = 0;
	u32 base_allocation_count = 0;
};

struct QueryComponentMetadata {
	ComponentTypeID component_type_id;
	u32 component_stream_index = 0;
};

struct QueryTypeMetadata {
	TypeInfoStruct* type_info = nullptr;
	ArrayView<QueryComponentMetadata> components;
};

void WriteEntitySystemMetadata(StackAllocator* alloc, ArrayView<TypeInfoStruct*> entity_type_infos, ArrayView<TypeInfoStruct*> entity_query_type_infos, HashTable<String, VersionedTypeInfo>& version_history) {
	HashTable<ComponentTypeInfoKey, u32> component_types;
	Array<ComponentTypeInfoKey> component_type_infos;
	
	Array<EntityTypeMetadata> entity_type_metadata;
	ArrayReserve(entity_type_metadata, alloc, entity_type_infos.count);
	
	u32 cpu_component_count = 0;
	u32 gpu_component_count = 0;
	
	for (auto* type_info : entity_type_infos) {
		Array<EntityComponentMetadata> entity_components;
		ArrayReserve(entity_components, alloc, type_info->fields.count);
		
		u64 entity_type_note_source_location = 0;
		auto* entity_type_note = FindNote<Meta::EntityType>(type_info, &entity_type_note_source_location);
		
		EntityTypeMetadata entity_type_info;
		entity_type_info.type_info = type_info;
		entity_type_info.base_allocation_count = entity_type_note->base_allocation_count;
		
		if (IsPowerOfTwo32(entity_type_info.base_allocation_count) == false) {
			ReportError(alloc, entity_type_note_source_location, "Entity 'base_allocation_count' must be a power of 2. Value given: '%'."_sl, entity_type_info.base_allocation_count);
		}
		
		for (auto& field : type_info->fields) {
			auto* component_type_note = FindNote<ComponentType>(field.type);
			if (component_type_note == nullptr) {
				auto type_name = PrintTypeName(alloc, field.type);
				ReportError(alloc, field.source_location, "Unexpected field '%' of type '%' used in entity type '%'. Only components are allowed."_sl, field.name, type_name, type_info->name);
			}
			
			auto* component_type_info = TypeInfoCast<TypeInfoStruct>(ExtractTemplateParameterType(field.type, 0));
			if (component_type_info == nullptr) {
				ReportError(alloc, field.source_location, "Template type of Component '%' in entity type '%' is not reflected."_sl, field.name, type_info->name);
			}
			
			EntityComponentMetadata component_metadata;
			component_metadata.type_info      = component_type_info;
			component_metadata.component_type = *component_type_note;
			
			if (component_metadata.component_type == ComponentType::GPU) {
				auto* note = FindNote<VirtualResourceID>(field);
				if (note == nullptr) {
					ReportError(alloc, field.source_location, "Missing 'VirtualResourceID' note on GPU component."_sl);
				}
				
				component_metadata.resource_id = *note;
				entity_type_info.gpu_component_count += 1;
			} else {
				entity_type_info.cpu_component_count += 1;
			}
			ArrayAppend(entity_components, component_metadata);
			
			
			ComponentTypeInfoKey component_type_info_key;
			component_type_info_key.type_info      = component_type_info;
			component_type_info_key.component_type = *component_type_note;
			component_type_info_key.generate_save_load_callback = FindNote<Meta::NoSaveLoad>(component_type_info) == nullptr;
			component_type_info_key.name_hash      = (u32)ComputeHash(component_type_info->name);
			
			auto [element, is_added] = HashTableAddOrFind(component_types, alloc, component_type_info_key, 0u);
			if (is_added) {
				ArrayAppend(component_type_infos, alloc, component_type_info_key);
				
				if (component_type_info_key.component_type == ComponentType::GPU) {
					gpu_component_count += 1;
				} else {
					cpu_component_count += 1;
				}
			}
		}
		entity_type_info.components = entity_components;
		
		ArrayAppend(entity_type_metadata, entity_type_info);
	}
	
	HeapSort<ComponentTypeInfoKey>(component_type_infos, [](auto& lh, auto& rh)-> bool {
		return lh.component_type == rh.component_type ? lh.name_hash < rh.name_hash : (u32)lh.component_type < (u32)rh.component_type;
	});
	
	auto cpu_component_type_infos = ArrayView<ComponentTypeInfoKey>{ component_type_infos.data, cpu_component_count };
	
	for (u32 i = 0; i < component_type_infos.count; i += 1) {
		auto* element = HashTableFind(component_types, component_type_infos[i]);
		element->value = i;
	}
	
	for (auto& entity_type_info : entity_type_metadata) {
		for (auto& component : entity_type_info.components) {
			auto* element = HashTableFind(component_types, { component.type_info, component.component_type });
			component.component_type_id = { element->value };
		}
		
		HeapSort(entity_type_info.components, [](auto& lh, auto& rh)-> bool {
			return lh.component_type_id.index < rh.component_type_id.index;
		});
	}
	
	Array<QueryTypeMetadata> query_type_metadata;
	ArrayReserve(query_type_metadata, alloc, entity_query_type_infos.count);
	
	for (auto* type_info : entity_query_type_infos) {
		Array<QueryComponentMetadata> component_type_infos;
		ArrayReserve(component_type_infos, alloc, type_info->fields.count);
		
		u32 stream_index = 0;
		for (auto& field : type_info->fields) {
			auto* component_type_note = FindNote<ComponentType>(field.type);
			auto* component_stream_type_info = TypeInfoCast<TypeInfoPointer>(field.type);
			if (component_type_note == nullptr && component_stream_type_info == nullptr) {
				auto type_name = PrintTypeName(alloc, field.type);
				ReportError(alloc, field.source_location, "Unexpected field '%' of type '%' used in entity query type '%'. Only components and pointers to components are allowed."_sl, field.name, type_name, type_info->name);
			}
			
			auto* component_type_info = TypeInfoCast<TypeInfoStruct>(component_type_note ? ExtractTemplateParameterType(field.type, 0) : component_stream_type_info->pointer_to);
			if (component_type_info == nullptr) {
				ReportError(alloc, field.source_location, "Template type of Component '%' in entity type '%' is not reflected."_sl, field.name, type_info->name);
			}
			
			ComponentTypeInfoKey component_type_info_key;
			component_type_info_key.type_info      = component_type_info;
			component_type_info_key.component_type = component_type_note ? *component_type_note : ComponentType::CPU;
			
			auto* element = HashTableFind(component_types, component_type_info_key);
			
			QueryComponentMetadata component_metadata;
			component_metadata.component_type_id.index = element ? element->value : u32_max;
			component_metadata.component_stream_index  = stream_index++;
			
			ArrayAppend(component_type_infos, component_metadata);
		}
		
		HeapSort<QueryComponentMetadata>(component_type_infos, [](auto& lh, auto& rh)-> bool {
			return lh.component_type_id.index < rh.component_type_id.index;
		});
		
		QueryTypeMetadata entity_query_type_info;
		entity_query_type_info.type_info  = type_info;
		entity_query_type_info.components = component_type_infos;
		ArrayAppend(query_type_metadata, entity_query_type_info);
	}
	
	StringBuilder builder;
	builder.alloc = alloc;
	builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	builder.Append("#include \"Basic/BasicSaveLoad.h\"\n"_sl);
	builder.Append("#include \"Engine/Entities.h\"\n\n"_sl);
	
	for (u32 entity_type_index = 0; entity_type_index < entity_type_infos.count; entity_type_index += 1) {
		auto* type_info = entity_type_infos[entity_type_index];
		builder.Append("EntityTypeID ECS::GetEntityTypeID<%>::id = { % };\n"_sl, type_info->name, entity_type_index);
	}
	builder.Append("\n"_sl);
	
	for (u32 entity_query_type_index = 0; entity_query_type_index < entity_query_type_infos.count; entity_query_type_index += 1) {
		auto* type_info = entity_query_type_infos[entity_query_type_index];
		builder.Append("EntityQueryTypeID ECS::GetEntityQueryTypeID<%>::id = { % };\n"_sl, type_info->name, entity_query_type_index);
	}
	builder.Append("\n"_sl);
	
	for (u32 component_type_index = 0; component_type_index < component_type_infos.count; component_type_index += 1) {
		auto* type_info = component_type_infos[component_type_index].type_info;
		builder.Append("ComponentTypeID ECS::GetComponentTypeID<%>::id = { % };\n"_sl, type_info->name, component_type_index);
	}
	builder.Append("\n"_sl);
	
	for (auto& runtime_type_info : entity_type_metadata) {
		builder.Append("static ComponentTypeID %_entity_component_type_ids[] = { "_sl, runtime_type_info.type_info->name);
		for (auto& component : runtime_type_info.components) {
			builder.Append("%, "_sl, component.component_type_id.index);
		}
		builder.Append("};\n"_sl);
		
		builder.Append("static u32 %_virtual_resource_ids[] = { "_sl, runtime_type_info.type_info->name);
		for (auto& component : runtime_type_info.components) {
			builder.Append("%, "_sl, (u32)component.resource_id);
		}
		builder.Append("};\n"_sl);
	}
	builder.Append("\n"_sl);
	
	for (auto& runtime_type_info : query_type_metadata) {
		builder.Append("static ComponentTypeID %_query_component_type_ids[] = { "_sl, runtime_type_info.type_info->name);
		for (auto& component : runtime_type_info.components) {
			builder.Append("%, "_sl, component.component_type_id.index);
		}
		builder.Append("};\n"_sl);
		
		builder.Append("static u32 %_component_stream_indices[] = { "_sl, runtime_type_info.type_info->name);
		for (auto& component : runtime_type_info.components) {
			builder.Append("%, "_sl, component.component_stream_index);
		}
		builder.Append("};\n"_sl);
	}
	builder.Append("\n"_sl);
	
	
	builder.Append("static EntityTypeInfo entity_type_info_table_internal[] = {\n"_sl);
	builder.Indent();
	for (auto& runtime_type_info : entity_type_metadata) {
		builder.Append("{ { %0_entity_component_type_ids, %1 }, { %0_virtual_resource_ids, %1 }, %2, %3, %4, 0x%5x },\n"_sl, runtime_type_info.type_info->name, runtime_type_info.components.count, runtime_type_info.cpu_component_count, runtime_type_info.gpu_component_count, runtime_type_info.base_allocation_count, ComputeHash(runtime_type_info.type_info->name));
	}
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	
	builder.Append("static EntityQueryTypeInfo entity_query_type_info_table_internal[] = {\n"_sl);
	builder.Indent();
	for (auto& runtime_type_info : query_type_metadata) {
		builder.Append("{ { %0_query_component_type_ids, %1 }, { %0_component_stream_indices, %1 } },\n"_sl, runtime_type_info.type_info->name, runtime_type_info.components.count);
	}
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	builder.Append("ComponentTypeInfo component_type_info_table_internal[] = {\n"_sl);
	builder.Indent();
	for (auto& component : component_type_infos) {
		u64 version = 0;
		if (component.component_type == ComponentType::CPU && component.generate_save_load_callback) {
			version = AddTypeInfoToSaveLoadHistory(alloc, version_history, component.type_info);
		}
		
		auto component_type_value = PrintTypeValue(alloc, TypeInfoOf<ComponentType>(), &component.component_type); 
		builder.Append("{ %, %, 0x%x, % },\n"_sl, component.type_info->size, version, ComputeHash(component.type_info->name), component_type_value);
	}
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	for (auto& component : cpu_component_type_infos) {
		if (component.generate_save_load_callback) {
			builder.Append("extern void SaveLoad(SaveLoadBuffer& buffer, %& component, u64 version);\n"_sl, component.type_info->name);
		}
	}
	builder.Append("\n"_sl);
	
	builder.Append("SaveLoadCallback component_save_load_callbacks_internal[] = {\n"_sl);
	builder.Indent();
	for (auto& component : cpu_component_type_infos) {
		if (component.generate_save_load_callback) {
			builder.Append("[](SaveLoadBuffer& buffer, void* data, u64 version) { SaveLoad(buffer, *(%*)data, version); },\n"_sl, component.type_info->name);
		} else {
			builder.Append("nullptr,\n"_sl);
		}
	}
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	builder.Append("DefaultInitializeCallback component_default_initialize_callbacks_internal[] = {\n"_sl);
	builder.Indent();
	for (auto& component : cpu_component_type_infos) {
		builder.Append("[](void* data, u64 begin, u64 end) { for (u64 i = begin; i < end; i += 1) ((%*)data)[i] = {}; },\n"_sl, component.type_info->name);
	}
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	
	builder.Append("ArrayView<EntityTypeInfo> entity_type_info_table = { entity_type_info_table_internal, % };\n"_sl, entity_type_infos.count);
	builder.Append("ArrayView<EntityQueryTypeInfo> entity_query_type_info_table = { entity_query_type_info_table_internal, % };\n"_sl, entity_query_type_infos.count);
	builder.Append("ArrayView<ComponentTypeInfo> component_type_info_table = { component_type_info_table_internal, % };\n"_sl, component_type_infos.count);
	builder.Append("ArrayView<SaveLoadCallback> component_save_load_callbacks = { component_save_load_callbacks_internal, % };\n"_sl, cpu_component_type_infos.count);
	builder.Append("ArrayView<DefaultInitializeCallback> component_default_initialize_callbacks = { component_default_initialize_callbacks_internal, % };\n"_sl, cpu_component_type_infos.count);
	
	WriteGeneratedFile(alloc, "Engine/Generated/EntitySystemMetadata.cpp"_sl, builder.ToString());
}
