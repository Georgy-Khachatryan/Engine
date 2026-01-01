#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "MetaprogramSystems.h"
#include "MetaprogramCommon.h"
#include "Tokens.h"

namespace Meta {
	struct HlslFile;
	struct RenderPass;
	struct ShaderName;
	struct EntityType;
	struct ComponentQuery;
}

template<typename NoteT, typename TypeInfoT>
static void ArrayAppendIfHasNote(Array<TypeInfoT*>& type_infos, StackAllocator* alloc, TypeInfo* type_info) {
	u64 source_location = 0;
	auto* note = FindNote<NoteT>(type_info, &source_location);
	
	if (note != nullptr) {
		auto* type_info_t = TypeInfoCast<TypeInfoT>(type_info);
		if (type_info_t == nullptr) {
			auto note_type_name = PrintTypeName(alloc, TypeInfoOf<NoteT>());
			ReportError(alloc, source_location, "Note '%' must be applied to a %.."_sl, note_type_name, type_info_type_names[(u32)TypeInfoT::my_type]);
		}
		ArrayAppend(type_infos, alloc, type_info_t);
	}
}

s32 main(s32 argument_count, const char* arguments[]) {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	if ((argument_count >= 2) && (strcmp(arguments[1], "-m") == 0)) {
		TempAllocationScope(&alloc);
		WriteCodeForMathLibrary(&alloc);
	}
	
	Array<TypeInfoStruct*> hlsl_file_type_infos;
	Array<TypeInfoStruct*> render_pass_type_infos;
	Array<TypeInfoEnum*>   shader_definition_type_infos;
	Array<TypeInfoStruct*> entity_type_infos;
	Array<TypeInfoStruct*> entity_query_type_infos;
	
	extern ArrayView<TypeInfo*> type_table;
	for (auto* type_info : type_table) {
		ArrayAppendIfHasNote<Meta::HlslFile>(hlsl_file_type_infos, &alloc, type_info);
		ArrayAppendIfHasNote<Meta::RenderPass>(render_pass_type_infos, &alloc, type_info);
		ArrayAppendIfHasNote<Meta::ShaderName>(shader_definition_type_infos, &alloc, type_info);
		ArrayAppendIfHasNote<Meta::EntityType>(entity_type_infos, &alloc, type_info);
		ArrayAppendIfHasNote<Meta::ComponentQuery>(entity_query_type_infos, &alloc, type_info);
	}
	
	EnsureDirectoryExists(&alloc, "Shaders/Generated/"_sl);
	EnsureDirectoryExists(&alloc, "Engine/Generated/"_sl);
	
	auto version_history = ParseSaveLoadVersionHistory(&alloc);
	WriteEntitySystemMetadata(&alloc, entity_type_infos, entity_query_type_infos, version_history);
	
	WriteSaveLoadCallbacks(&alloc, version_history);
	WriteSaveLoadVersionHistory(&alloc, version_history);
	
	WriteCodeForShaderDefinitions(&alloc, shader_definition_type_infos);
	WriteCodeForRenderPasses(&alloc, hlsl_file_type_infos, render_pass_type_infos);
	
	return 0;
}


void ReportErrorV(StackAllocator* alloc, u64 source_location, String format, ArrayView<StringFormatArgument> arguments) {
	extern ArrayView<TypeInfoSourceFile> source_file_table;
	
	u64 file_index = (source_location >> 48);
	u64 length = (source_location >> 32) & u16_max;
	u64 offset = (source_location >> 0 ) & u32_max;
	
	auto source_file = source_file_table[file_index];
	auto file = SystemReadFileToString(alloc, source_file.filepath);
	
	ErrorReportContext error_context;
	error_context.file       = file;
	error_context.filepath   = source_file_table[file_index].filepath;
	error_context.file_index = file_index;
	
	if (file.data == nullptr) {
		error_context.ReportMessage(alloc, {}, "Failed to open source file. The following error source location will be incorrect."_sl);
	} else if (source_file.hash != ComputeHash(file)) {
		error_context.ReportMessage(alloc, {}, "Source file hash mismatch. The following error source location might be incorrect."_sl);
	}
	
	error_context.ReportErrorV(alloc, String{ file.data + offset, length }, format, arguments);
}

void CheckFieldIsReflected(StackAllocator* alloc, TypeInfoStruct* type_info, TypeInfoStructField& field) {
	if (field.type == nullptr) {
		ReportError(alloc, field.source_location, "Type of field '%' in struct '%' is not reflected."_sl, field.name, type_info->name);
	}
}
