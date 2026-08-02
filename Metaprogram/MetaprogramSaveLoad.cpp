#include "Basic/Basic.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "MetaprogramSystems.h"
#include "MetaprogramCommon.h"
#include "TypeInfo.h"
#include "Tokens.h"

compile_const auto save_load_versions_filepath = "Engine/SaveLoadVersions.txt"_sl;

struct VersionedField {
	String name;
	
	String type_name;
	
	u64 type_version   = 0;
	u64 constant_value = 0;
	
	u64 hash = 0;
};

struct VersionedTypeInfo {
	TypeInfoType info_type = TypeInfoType::None;
	u32 template_parameter_count = 0;
	
	struct VersionInfo {
		u64 version = 0;
		u64 hash    = 0;
		String underlying_type;
		ArrayView<VersionedField> fields;
	};
	
	Array<VersionInfo> versions;
	
	TypeInfo* current_version_type_info = nullptr;
};

static u64 ComputeHash(const VersionedField& field, bool is_enum) {
	return ComputeHash64(ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)), is_enum ? field.constant_value : field.type_version);
}


static u64 AddVersionedTypeToSaveLoadHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo>& version_history, String name, TypeInfo* type_info, VersionedTypeInfo::VersionInfo new_version, u32 template_parameter_count = 0) {
	auto [element, is_added] = HashTableAddOrFind(version_history, alloc, name, { type_info->info_type, template_parameter_count });
	
	auto& type = element->value;
	type.current_version_type_info = type_info;
	
	if (is_added || ArrayLastElement(type.versions).hash != new_version.hash) {
		new_version.version = is_added ? 0 : ArrayLastElement(type.versions).version + 1;
		ArrayAppend(type.versions, alloc, new_version);
	}
	
	return ArrayLastElement(type.versions).version;
}

u64 AddTypeInfoToSaveLoadHistory(StackAllocator* alloc, u64 source_location, HashTable<String, VersionedTypeInfo>& version_history, TypeInfo* type_info) {
	u64 result_version = 0;
	
	if (type_info->info_type == TypeInfoType::Struct) {
		auto* type_info_struct = (TypeInfoStruct*)type_info;
		
		auto type_name = PrintTypeName(alloc, type_info_struct);
		
		VersionedTypeInfo::VersionInfo info;
		info.hash = ComputeHash(type_name);
		
		u32 template_parameter_count = 0;
		
		Array<VersionedField> fields;
		ArrayReserve(fields, alloc, type_info_struct->fields.count);
		for (auto& field : type_info_struct->fields) {
			template_parameter_count += HasAnyFlags(field.flags, TypeInfoStructFieldFlags::TemplateParameter) ? 1u : 0u;
			
			if (field.type == &type_info_type || field.constant_value) continue;
			
			CheckFieldIsReflected(alloc, type_info_struct, field);
			
			u64 type_version = AddTypeInfoToSaveLoadHistory(alloc, field.source_location, version_history, field.type);
			
			VersionedField version_field;
			version_field.name           = field.name;
			version_field.type_name      = PrintTypeName(alloc, field.type);
			version_field.type_version   = type_version;
			version_field.constant_value = 0;
			version_field.hash           = ComputeHash(version_field, false);
			ArrayAppend(fields, alloc, version_field);
			
			info.hash = ComputeHash64(version_field.hash, info.hash);
		}
		
		info.fields = fields;
		
		result_version = AddVersionedTypeToSaveLoadHistory(alloc, version_history, type_name, type_info_struct, info, template_parameter_count);
	} else if (type_info->info_type == TypeInfoType::Enum) {
		auto* type_info_enum = (TypeInfoEnum*)type_info;
		
		auto type_name = PrintTypeName(alloc, type_info_enum->underlying_type);
		
		VersionedTypeInfo::VersionInfo info;
		info.hash = ComputeHash(type_info_enum->name);
		info.underlying_type = type_name;
		
		Array<VersionedField> fields;
		ArrayReserve(fields, alloc, type_info_enum->fields.count);
		
		for (auto& field : type_info_enum->fields) {
			VersionedField version_field;
			version_field.name           = field.name;
			version_field.type_name      = type_name;
			version_field.type_version   = 0;
			version_field.constant_value = field.value;
			version_field.hash           = ComputeHash(version_field, true);
			ArrayAppend(fields, alloc, version_field);
			
			info.hash = ComputeHash64(version_field.hash, info.hash);
		}
		
		info.fields = fields;
		
		result_version = AddVersionedTypeToSaveLoadHistory(alloc, version_history, type_info_enum->name, type_info_enum, info);
	} else if (type_info->info_type == TypeInfoType::Array) {
		auto* type_info_array = (TypeInfoArray*)type_info;
		
		if (type_info_array->array_of == nullptr) {
			ReportError(alloc, source_location, "Array element type is not reflected."_sl);
		}
		
		result_version = AddTypeInfoToSaveLoadHistory(alloc, source_location, version_history, type_info_array->array_of);
	} else if (type_info->info_type == TypeInfoType::Integer) {
	} else if (type_info->info_type == TypeInfoType::Float) {
	} else if (type_info->info_type == TypeInfoType::String) {
	} else {
		ReportError(alloc, source_location, "Cannot SaveLoad type '%'."_sl, PrintTypeName(alloc, type_info));
	}
	
	return result_version;
}


void WriteSaveLoadCallbacks(StackAllocator* alloc, HashTable<String, VersionedTypeInfo> version_history) {
	StringBuilder builder;
	builder.alloc = alloc;
	builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	builder.Append("#include \"Basic/BasicSaveLoad.h\"\n"_sl);
	
	{
		HashTable<u64, void> include_files;
		HashTableReserve(include_files, alloc, 16);
		
		for (auto& [name, type] : version_history) {
			if (type.current_version_type_info == nullptr) continue;
			
			u64 source_location = 0;
			if (type.current_version_type_info->info_type == TypeInfoType::Enum) {
				auto* type_info_enum = (TypeInfoEnum*)type.current_version_type_info;
				source_location = type_info_enum->source_location;
			} else if (type.current_version_type_info->info_type == TypeInfoType::Struct) {
				auto* type_info_struct = (TypeInfoStruct*)type.current_version_type_info;
				source_location = type_info_struct->source_location;
			}
			
			auto [file_index, length, offset] = DecodeSourceLocation(source_location);
			HashTableAddOrFind(include_files, alloc, file_index);
		}
		
		extern ArrayView<TypeInfoSourceFile> source_file_table;
		for (auto [file_index] : include_files) {
			builder.Append("#include \"%\"\n"_sl, source_file_table[file_index].filepath);
		}
		builder.Append("\n"_sl);
	}
	
	
	// Create forward declarations for any removed types. They are only used for function overload resolution.
	for (auto& [name, type] : version_history) {
		if (type.current_version_type_info != nullptr) continue;
		
		if (type.template_parameter_count != 0) {
			Array<String> template_parameters;
			ArrayReserve(template_parameters, alloc, type.template_parameter_count);
			
			for (u32 i = 0; i < type.template_parameter_count; i += 1) {
				ArrayAppend(template_parameters, StringFormat(alloc, "typename T%"_sl, i));
			}
			builder.Append("template<%> "_sl, StringJoin(alloc, template_parameters, ", "_sl));
		}
		
		if (type.info_type == TypeInfoType::Enum) {
			builder.Append("enum struct % : %;\n"_sl, name, ArrayLastElement(type.versions).underlying_type);
		} else {
			builder.Append("struct %;\n"_sl, StringSplitByCharFromLeft(name, '<'));
		}
	}
	builder.Append("\n"_sl);
	
	
	for (auto& [name, type] : version_history) {
		builder.Append("void SaveLoad(SaveLoadBuffer& buffer, %& data, u64 version);\n"_sl, name);
	}
	builder.Append("\n"_sl);
	
	
	for (auto& [name, type] : version_history) {
		bool is_enum = type.info_type == TypeInfoType::Enum;
		auto& latest_version = ArrayLastElement(type.versions);
		
		builder.Append("void SaveLoad(SaveLoadBuffer& buffer, %& data, u64 version) {\n"_sl, name);
		
		HashTable<u64, u64> new_field_table;
		HashTableReserve(new_field_table, alloc, latest_version.fields.count);
		
		auto compute_field_hash = [](const VersionedField& field)-> u64 {
			return ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name));
		};
		
		for (auto& field : latest_version.fields) {
			HashTableAddOrFind(new_field_table, compute_field_hash(field), field.constant_value);
		}
		
		builder.Indent();
		
		for (s64 i = type.versions.count - 1; i >= 0; i -= 1) {
			auto& version = type.versions[i];
			
			bool is_latest_version = (&version == &latest_version);
			
			if (type.versions.count != 1) {
				if (is_latest_version) {
					if (type.current_version_type_info != nullptr) {
						builder.Append("DebugAssert(version == % || buffer.direction == SaveLoadDirection::Loading, \"Old versions can't be saved.\");\n"_sl, version.version);
					} else {
						builder.Append("DebugAssert(buffer.direction == SaveLoadDirection::Loading, \"Old versions can't be saved.\");\n"_sl);
					}
					builder.Append("if (version == %) {\n"_sl, version.version);
				} else {
					builder.Append("} else if (version == %) {\n"_sl, version.version);
				}
				
				builder.Indent();
			}
			
			if (is_enum) {
				if (is_latest_version && type.current_version_type_info != nullptr) {
					builder.Append("buffer.SaveLoadBytes(&data, sizeof(data));\n"_sl);
				} else {
					builder.Append("% value = 0;\n"_sl, version.underlying_type);
					builder.Append("SaveLoad(buffer, value);\n"_sl);
					
					if (type.current_version_type_info != nullptr) {
						builder.Append("switch (value) {\n"_sl);
						for (auto& field : version.fields) {
							auto* new_field = HashTableFind(new_field_table, compute_field_hash(field));
							
							if (new_field != nullptr) {
								builder.Append("case %: data = (%)%; break; // '%'\n"_sl, field.constant_value, name, new_field->value, field.name);
							} else {
								builder.Append("// Skipping '%'\n"_sl, field.name);
							}
						}
						builder.Append("default: data = {}; break;\n"_sl);
						builder.Append("}\n"_sl);
					}
				}
			} else {
				if (is_latest_version == false && type.current_version_type_info != nullptr) {
					// Default initialize when loading old data (might have incomplete set of fields).
					builder.Append("data = {};\n"_sl);
				}
				
				for (auto& field : version.fields) {
					bool has_new_field = HashTableFind(new_field_table, compute_field_hash(field)) != nullptr;
					
					if (has_new_field && type.current_version_type_info != nullptr) {
						builder.Append("SaveLoad(buffer, data.%, %);\n"_sl, field.name, field.type_version);
					} else {
						builder.Append("SaveLoadDummy<%>(buffer, %);\n"_sl, field.type_name, field.type_version);
					}
				}
			}
			
			builder.Unindent();
		}
		
		if (type.versions.count != 1) {
			builder.Append("}\n"_sl);
			builder.Unindent();
		}
		
		builder.Append("}\n\n"_sl);
	}
	
	WriteGeneratedFile(alloc, "Engine/Generated/SaveLoadCallbacks.cpp"_sl, builder.ToString());
}


HashTable<String, VersionedTypeInfo> ParseSaveLoadVersionHistory(StackAllocator* alloc) {
	auto file = SystemReadFileToString(alloc, save_load_versions_filepath);
	if (file.data == nullptr) return {};
	
	Tokenizer tokenizer;
	tokenizer.error_context.file     = file;
	tokenizer.error_context.filepath = save_load_versions_filepath;
	tokenizer.string   = file.data;
	tokenizer.alloc    = alloc;
	
	HashTable<String, VersionedTypeInfo> version_history;
	HashTableReserve(version_history, alloc, 128);
	
	auto token = tokenizer.PeekNextToken();
	while (token.type != TokenType::None) {
		auto info_type = tokenizer.ExpectToken(TokenType::Keyword);
		if (info_type.keyword != KeywordType::Enum && info_type.keyword != KeywordType::Struct) {
			tokenizer.ReportError(info_type, "Unexpected keyword, expected 'struct' or 'enum'."_sl);
		}
		bool is_enum = info_type.keyword == KeywordType::Enum;
		
		auto identifier = tokenizer.ExpectToken(TokenType::Identifier);
		u32 template_parameter_count = 0;
		
		token = tokenizer.PeekNextToken();
		if (is_enum == false && token.type == TokenType::Less) {
			token = SkipTokensWithNestingTracking(tokenizer, TokenType::Less, TokenType::Greater);
			identifier.string.count = token.string.data + token.string.count - identifier.string.data;
			
			auto paramter_count = tokenizer.ExpectToken(TokenType::Number);
			template_parameter_count = (u32)StringToU64(paramter_count.string);
		}
		
		tokenizer.ExpectToken(TokenType::OpeningBrace);
		
		auto [element, is_added] = HashTableAddOrFind(version_history, alloc, identifier.string, { is_enum ? TypeInfoType::Enum : TypeInfoType::Struct });
		if (is_added == false) {
			tokenizer.error_context.ReportMessage(alloc, identifier.string, "Type already exists."_sl);
			tokenizer.error_context.ReportError(alloc, element->key, "Previous declaration here."_sl);
		}
		
		element->value.template_parameter_count = template_parameter_count;
		
		auto& versions = element->value.versions;
		
		u64 identifier_hash = ComputeHash(identifier.string);
		
		token = tokenizer.PeekNextToken();
		while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
			auto version         = tokenizer.ExpectToken(TokenType::Number);
			auto underlying_type = is_enum ? tokenizer.ExpectToken(TokenType::Identifier) : Token{};
			
			tokenizer.ExpectToken(TokenType::OpeningBrace);
			
			u64 hash = identifier_hash;
			Array<VersionedField> fields;
			
			token = tokenizer.PeekNextToken();
			while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
				auto identifier = tokenizer.ExpectToken(TokenType::Identifier);
				auto type_name  = is_enum ? underlying_type : tokenizer.ExpectToken(TokenType::Identifier);
				
				token = tokenizer.PeekNextToken();
				if (is_enum == false && token.type == TokenType::Less) {
					token = SkipTokensWithNestingTracking(tokenizer, TokenType::Less, TokenType::Greater);
					type_name.string.count = token.string.data + token.string.count - type_name.string.data;
				}
				
				auto type_version_or_constant_value = tokenizer.ExpectToken(TokenType::Number);
				
				tokenizer.ExpectToken(TokenType::Semicolon);
				
				VersionedField field;
				field.name           = identifier.string;
				field.type_name      = type_name.string;
				field.type_version   = is_enum ? 0 : StringToU64(type_version_or_constant_value.string);
				field.constant_value = is_enum ? StringToU64(type_version_or_constant_value.string) : 0;
				field.hash           = ComputeHash(field, is_enum);
				ArrayAppend(fields, alloc, field);
				
				hash = ComputeHash64(field.hash, hash);
				
				token = tokenizer.PeekNextToken();
			}
			tokenizer.ExpectToken(TokenType::ClosingBrace);
			
			VersionedTypeInfo::VersionInfo version_info;
			version_info.version = StringToU64(version.string);
			version_info.hash    = hash;
			version_info.underlying_type = underlying_type.string;
			version_info.fields  = fields;
			ArrayAppend(versions, alloc, version_info);
			
			token = tokenizer.PeekNextToken();
		}
		tokenizer.ExpectToken(TokenType::ClosingBrace);
		
		if (versions.count == 0) {
			tokenizer.ReportError(identifier, "Empty versioned type has no versions defined."_sl);
		}
		
		// Sort the latest version to the end of the array.
		HeapSort<VersionedTypeInfo::VersionInfo>(versions, [](auto& lh, auto& rh)-> bool {
			return lh.version < rh.version;
		});
		
		token = tokenizer.PeekNextToken();
	}
	
	return version_history;
}

void WriteSaveLoadVersionHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo> version_history) {
	Array<HashTableElement<String, VersionedTypeInfo>> sorted_version_history;
	ArrayReserve(sorted_version_history, alloc, version_history.count);
	
	for (auto& element : version_history) {
		ArrayAppend(sorted_version_history, element);
	}
	
	HeapSort<HashTableElement<String, VersionedTypeInfo>>(sorted_version_history, [](auto& lh, auto& rh)-> bool {
		return StringCompare(lh.key, rh.key) < 0;
	});
	
	
	StringBuilder builder;
	builder.alloc = alloc;
	
	builder.Append("// Generated SaveLoad Version History:\n\n"_sl);
	
	for (auto& [name, type] : sorted_version_history) {
		bool is_enum = type.info_type == TypeInfoType::Enum;
		
		builder.Append(type.template_parameter_count ? "% % % {\n"_sl : "% % {\n"_sl, is_enum ? "enum"_sl : "struct"_sl, name, type.template_parameter_count);
		builder.Indent();
		
		for (auto& version : type.versions) {
			builder.Append(is_enum ? "/*Version*/ % % {\n"_sl : "/*Version*/ % {\n"_sl, version.version, version.underlying_type);
			builder.Indent();
			
			for (auto& field : version.fields) {
				builder.Append(is_enum ? "%0 %3;\n"_sl : "%0 %1 %2;\n"_sl, field.name, field.type_name, field.type_version, field.constant_value);
			}
			
			builder.Unindent();
			builder.Append("}\n"_sl);
		}
		
		builder.Unindent();
		builder.Append("}\n\n"_sl);
	}
	
	WriteGeneratedFile(alloc, save_load_versions_filepath, builder.ToString());
}
