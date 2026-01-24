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
	bool generate_save_load_callback = false;
	
	struct VersionInfo {
		u64 version = 0;
		u64 hash    = 0;
		String underlying_type;
		ArrayView<VersionedField> fields;
	};
	
	Array<VersionInfo> versions;
};


static u64 AddVersionedTypeToSaveLoadHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo>& version_history, String name, TypeInfoType info_type, VersionedTypeInfo::VersionInfo new_version) {
	auto [element, is_added] = HashTableAddOrFind(version_history, alloc, name, { info_type });
	
	auto& type = element->value;
	type.generate_save_load_callback = true;
	
	if (is_added || ArrayLastElement(type.versions).hash != new_version.hash) {
		new_version.version = is_added ? 0 : ArrayLastElement(type.versions).version + 1;
		ArrayAppend(type.versions, alloc, new_version);
	}
	
	return ArrayLastElement(type.versions).version;
}

u64 AddTypeInfoToSaveLoadHistory(StackAllocator* alloc, HashTable<String, VersionedTypeInfo>& version_history, TypeInfo* type_info) {
	u64 result_version = 0;
	
	if (type_info->info_type == TypeInfoType::Struct) {
		auto* type_info_struct = (TypeInfoStruct*)type_info;
		
		VersionedTypeInfo::VersionInfo info;
		info.hash = ComputeHash(type_info_struct->name);
		
		Array<VersionedField> fields;
		ArrayReserve(fields, alloc, type_info_struct->fields.count);
		for (auto& field : type_info_struct->fields) {
			if (field.type == &type_info_type || field.constant_value) continue;
			
			CheckFieldIsReflected(alloc, type_info_struct, field);
			
			if (field.type->info_type == TypeInfoType::Pointer) {
				auto type_name = PrintTypeName(alloc, field.type);
				ReportError(alloc, field.source_location, "Cannot SaveLoad field '%' of pointer type '% in struct '%'."_sl, field.name, type_name, type_info_struct->name);
			}
			
			u64 type_version = AddTypeInfoToSaveLoadHistory(alloc, version_history, field.type);
			
			VersionedField version_field;
			version_field.name           = field.name;
			version_field.type_name      = PrintTypeName(alloc, field.type);
			version_field.type_version   = type_version;
			version_field.constant_value = 0;
			version_field.hash           = ComputeHash64(ComputeHash64(ComputeHash(version_field.name), ComputeHash(version_field.type_name)), version_field.type_version);
			ArrayAppend(fields, alloc, version_field);
			
			info.hash = ComputeHash64(version_field.hash, info.hash);
		}
		
		info.fields = fields;
		
		result_version = AddVersionedTypeToSaveLoadHistory(alloc, version_history, type_info_struct->name, TypeInfoType::Struct, info);
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
			version_field.hash           = ComputeHash64(ComputeHash64(ComputeHash(version_field.name), ComputeHash(version_field.type_name)), version_field.constant_value);
			ArrayAppend(fields, alloc, version_field);
			
			info.hash = ComputeHash64(version_field.hash, info.hash);
		}
		
		info.fields = fields;
		
		result_version = AddVersionedTypeToSaveLoadHistory(alloc, version_history, type_info_enum->name, TypeInfoType::Enum, info);
	}
	
	return result_version;
}


void WriteSaveLoadCallbacks(StackAllocator* alloc, HashTable<String, VersionedTypeInfo> version_history) {
	StringBuilder builder;
	builder.alloc = alloc;
	builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	builder.Append("#include \"Basic/BasicSaveLoad.h\"\n"_sl);
	builder.Append("#include \"Engine/Entities.h\"\n\n"_sl);
	
	compile_const auto save_load_dummy_suffix = "_Dummy"_sl;
	
	// Create dummy enum struct types for any removed types. They are only used for function overload resolution.
	for (auto& [name, type] : version_history) {
		if (type.generate_save_load_callback == false) {
			builder.Append("enum struct %.% : u32;\n"_sl, name, save_load_dummy_suffix);
		}
	}
	
	for (auto& [name, type] : version_history) {
		auto suffix = type.generate_save_load_callback ? ""_sl : "_Dummy"_sl;
		builder.Append("void SaveLoad(SaveLoadBuffer& buffer, %.%& data, u64 version);\n"_sl, name, suffix);
	}
	builder.Append("\n"_sl);
	
	
	for (auto& [name, type] : version_history) {
		bool is_enum = type.info_type == TypeInfoType::Enum;
		
		auto suffix = type.generate_save_load_callback ? ""_sl : save_load_dummy_suffix;
		builder.Append("void SaveLoad(SaveLoadBuffer& buffer, %.%& data, u64 version) {\n"_sl, name, suffix);
		
		HashTable<u64, u64> new_field_table;
		HashTableReserve(new_field_table, alloc, ArrayLastElement(type.versions).fields.count);
		
		for (auto& field : ArrayLastElement(type.versions).fields) {
			HashTableAddOrFind(new_field_table, ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)), field.constant_value);
		}
		
		builder.Indent();
		
		for (s64 i = type.versions.count - 1; i >= 0; i -= 1) {
			auto& version = type.versions[i];
			
			bool is_latest_version = i == (type.versions.count - 1);
			
			if (type.versions.count != 1) {
				if (is_latest_version) {
					if (type.generate_save_load_callback) {
						builder.Append("DebugAssert(version == % || buffer.is_loading, \"Old versions can only be loaded.\");\n"_sl, version.version);
					} else {
						builder.Append("DebugAssert(buffer.is_loading, \"Old versions can only be loaded.\");\n"_sl);
					}
					builder.Append("if (version == %) {\n"_sl, version.version);
				} else {
					builder.Append("} else if (version == %) {\n"_sl, version.version);
				}
				
				builder.Indent();
			}
			
			if (is_enum) {
				if (is_latest_version && type.generate_save_load_callback) {
					builder.Append("buffer.SaveLoadBytes(&data, sizeof(data));\n"_sl);
				} else {
					builder.Append("% value = 0;\n"_sl, version.underlying_type);
					builder.Append("SaveLoad(buffer, value);\n"_sl);
					
					if (type.generate_save_load_callback) {
						builder.Append("switch (value) {\n"_sl);
						for (auto& field : version.fields) {
							auto* new_field = HashTableFind(new_field_table, ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)));
							
							if (new_field != nullptr) {
								builder.Append("case %: data = (%)%; break;\n"_sl, field.constant_value, name, new_field->value);
							}
						}
						builder.Append("default: data = {}; break;\n"_sl);
						builder.Append("}\n"_sl);
					}
				}
			} else {
				if (is_latest_version == false && type.generate_save_load_callback) {
					// Default initialize when loading old data (might have incomplete set of fields).
					builder.Append("data = {};\n"_sl);
				}
				
				for (auto& field : version.fields) {
					bool has_new_field = HashTableFind(new_field_table, ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name))) != nullptr;
					
					if (has_new_field && type.generate_save_load_callback) {
						builder.Append("SaveLoad(buffer, data.%, %);\n"_sl, field.name, field.type_version);
					} else {
						auto* element = HashTableFind(version_history, field.type_name);
						bool use_skip_callback = (element != nullptr) && (element->value.generate_save_load_callback == false);
						
						auto suffix = use_skip_callback ? save_load_dummy_suffix : ""_sl;
						builder.Append("SaveLoadDummy<%.%>(buffer, %);\n"_sl, field.type_name, suffix, field.type_version);
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
		tokenizer.ExpectToken(TokenType::OpeningBrace);
		
		auto [element, is_added] = HashTableAddOrFind(version_history, alloc, identifier.string, { is_enum ? TypeInfoType::Enum : TypeInfoType::Struct });
		if (is_added == false) {
			tokenizer.error_context.ReportMessage(alloc, identifier.string, "Type already exists."_sl);
			tokenizer.error_context.ReportError(alloc, element->key, "Previous declaration here."_sl);
		}
		
		auto& versions = element->value.versions;
		
		u64 latest_version = 0;
		u64 latest_version_index = 0;
		
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
				auto type_version_or_constant_value = tokenizer.ExpectToken(TokenType::Number);
				
				tokenizer.ExpectToken(TokenType::Semicolon);
				
				VersionedField field;
				field.name           = identifier.string;
				field.type_name      = type_name.string;
				field.type_version   = is_enum ? 0 : StringToU64(type_version_or_constant_value.string);
				field.constant_value = is_enum ? StringToU64(type_version_or_constant_value.string) : 0;
				field.hash           = ComputeHash64(ComputeHash64(ComputeHash(field.name), ComputeHash(field.type_name)), is_enum ? field.constant_value : field.type_version);
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
			
			if (version_info.version > latest_version) {
				latest_version       = version_info.version;
				latest_version_index = versions.count - 1;
			}
			
			token = tokenizer.PeekNextToken();
		}
		tokenizer.ExpectToken(TokenType::ClosingBrace);
		
		if (element->value.versions.count == 0) {
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
	StringBuilder builder;
	builder.alloc = alloc;
	
	builder.Append("// Generated SaveLoad Version History:\n\n"_sl);
	
	for (auto& [name, type] : version_history) {
		bool is_enum = type.info_type == TypeInfoType::Enum;
		
		builder.Append("% % {\n"_sl, is_enum ? "enum"_sl : "struct"_sl, name);
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
