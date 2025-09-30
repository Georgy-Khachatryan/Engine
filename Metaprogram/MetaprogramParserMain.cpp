#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Tokens.h"
#include "AstNodes.h"


static String NamespaceStackToString(Tokenizer& tokenizer) {
	return StringJoin(tokenizer.alloc, tokenizer.namespace_stack, "::"_sl);
}

static AstNodeDeclaration* ParseDeclaration(Tokenizer& tokenizer);

static Token SkipTokensWithNestingTracking(Tokenizer& tokenizer, TokenType opening_token, TokenType closing_token) {
	tokenizer.ExpectToken(opening_token);
	auto token = tokenizer.PeekNextToken();
	
	s32 nesting_depth = 1;
	while (nesting_depth != 0) {
		token = tokenizer.FindNextToken();
		
		if (token.type == opening_token) {
			nesting_depth += 1;
		} else if (token.type == closing_token) {
			nesting_depth -= 1;
		} else if (token.type == TokenType::None) {
			nesting_depth = 0;
		}
	}
	
	if (token.type != closing_token) {
		tokenizer.ReportError(token, "Unexpected end of file in a list.");
	}
	
	return token;
}

static Token SkipTokens(Tokenizer& tokenizer, TokenType opening_token, TokenType closing_token) {
	auto token = tokenizer.ExpectToken(opening_token);
	
	while (token.type != TokenType::None && token.type != closing_token) {
		token = tokenizer.FindNextToken();
	}
	
	if (token.type != closing_token) {
		tokenizer.ReportError(token, "Unexpected end of file in a list.");
	}
	
	return token;
}

static Token ParseIdentifierWithNamespace(Tokenizer& tokenizer) {
	auto token_0 = tokenizer.ExpectToken(TokenType::Identifier);
	
	auto token_1 = tokenizer.PeekNextToken();
	while (token_1.type == TokenType::DoubleColon) {
		tokenizer.FindNextToken();
		tokenizer.ExpectToken(TokenType::Identifier);
		token_1 = tokenizer.PeekNextToken();
	}
	
	token_0.string.count = token_1.string.data - token_0.string.data;
	while (token_0.string.count != 0 && token_0.string[token_0.string.count - 1] == ' ') {
		token_0.string.count -= 1;
	}
	
	return token_0;
}

static AstNoteInfo ParseNote(Tokenizer& tokenizer) {
	auto token_0 = ParseIdentifierWithNamespace(tokenizer);
	auto token_1 = tokenizer.PeekNextToken();
	
	if (token_1.type == TokenType::OpeningBrace) {
		token_1 = SkipTokensWithNestingTracking(tokenizer, TokenType::OpeningBrace, TokenType::ClosingBrace);
		token_1 = tokenizer.PeekNextToken();
	}
	
	AstNoteInfo note;
	note.type_name        = token_0.string;
	note.expression.data  = token_0.string.data;
	note.expression.count = token_1.string.data - token_0.string.data;
	
	return note;
}

static AstNodeNotes* ParseNotes(Tokenizer& tokenizer) {
	tokenizer.ExpectKeyword(KeywordType::Notes);
	tokenizer.ExpectToken(TokenType::OpeningParen);
	
	Array<AstNoteInfo> notes;
	
	auto token = tokenizer.PeekNextToken();
	while (token.type != TokenType::None && token.type != TokenType::ClosingParen) {
		auto note = ParseNote(tokenizer);
		ArrayAppend(notes, tokenizer.alloc, note);
		
		token = tokenizer.PeekNextToken();
		if (token.type != TokenType::Comma && token.type != TokenType::ClosingParen) {
			tokenizer.ReportError(token, "Unexpected token. Expected comma or closing paren.");
		}
		
		if (token.type == TokenType::Comma) {
			tokenizer.FindNextToken();
			token = tokenizer.PeekNextToken();
		}
	}
	
	tokenizer.ExpectToken(TokenType::ClosingParen);
	
	AstNodeNotes* node = nullptr;
	if (notes.count != 0) {
		node = AstNew<AstNodeNotes>(tokenizer.alloc);
		node->notes = notes;
	}
	
	return node;
}

static AstNodeCodeBlock* ParseTemplate(Tokenizer& tokenizer) {
	tokenizer.ExpectKeyword(KeywordType::Template);
	tokenizer.ExpectToken(TokenType::Less);
	auto token_0 = tokenizer.PeekNextToken();
	
	auto* code_block = AstNew<AstNodeCodeBlock>(tokenizer.alloc);
	
	Array<AstNodeDeclaration*> declarations;
	
	auto token = token_0;
	while (token.type != TokenType::None && token.type != TokenType::Greater) {
		tokenizer.ExpectKeyword(KeywordType::Typename);
		auto name = tokenizer.ExpectToken(TokenType::Identifier);
		
		auto* declaration = AstNew<AstNodeDeclaration>(tokenizer.alloc);
		declaration->name = name.string;
		declaration->declaration_type = AstNodeDeclarationType::Typename;
		ArrayAppend(declarations, tokenizer.alloc, declaration);
		
		token = tokenizer.PeekNextToken();
		if (token.type == TokenType::Comma) {
			tokenizer.FindNextToken();
			token = tokenizer.PeekNextToken();
		}
	}
	
	auto token_1 = tokenizer.ExpectToken(TokenType::Greater);
	
	code_block->namespace_path = NamespaceStackToString(tokenizer);
	code_block->declarations = declarations;
	code_block->declared_namespace.data  = token_0.string.data;
	code_block->declared_namespace.count = token_1.string.data - token_0.string.data;
	
	return code_block;
}

static AstNodeStruct* ParseStruct(Tokenizer& tokenizer, AstNodeNotes* notes) {
	tokenizer.ExpectKeyword(KeywordType::Struct);
	
	auto name = tokenizer.ExpectToken(TokenType::Identifier);
	ArrayAppend(tokenizer.namespace_stack, tokenizer.alloc, name.string);
	defer{ ArrayPopLast(tokenizer.namespace_stack); };
	
	auto token = tokenizer.PeekNextToken();
	if (token.type == TokenType::Colon) {
		tokenizer.FindNextToken();
		ParseIdentifierWithNamespace(tokenizer);
	}
	
	tokenizer.ExpectToken(TokenType::OpeningBrace);
	
	token = tokenizer.PeekNextToken();
	if (token.type == TokenType::Identifier && token.string == "RENDER_PASS_GENERATED_CODE"_sl) {
		tokenizer.ExpectToken(TokenType::Identifier);
		tokenizer.ExpectToken(TokenType::OpeningParen);
		tokenizer.ExpectToken(TokenType::ClosingParen);
		tokenizer.ExpectToken(TokenType::Semicolon);
		token = tokenizer.PeekNextToken();
	}
	
	auto* code_block = AstNew<AstNodeCodeBlock>(tokenizer.alloc);
	code_block->namespace_path     = NamespaceStackToString(tokenizer);
	code_block->declared_namespace = name.string;
	
	Array<AstNodeDeclaration*> declarations;
	while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
		auto* declaration = ParseDeclaration(tokenizer);
		ArrayAppend(declarations, tokenizer.alloc, declaration);
		
		token = tokenizer.PeekNextToken();
	}
	code_block->declarations = declarations;
	
	tokenizer.ExpectToken(TokenType::ClosingBrace);
	
	auto* ast_node_struct = AstNew<AstNodeStruct>(tokenizer.alloc);
	ast_node_struct->name = name.string;
	ast_node_struct->code_block = code_block;
	ast_node_struct->notes = notes;
	
	return ast_node_struct;
}

static AstNodeDeclaration* ParseDeclaration(Tokenizer& tokenizer) {
	auto token = tokenizer.PeekNextToken();
	
	AstNodeNotes* notes = nullptr;
	if (token.keyword == KeywordType::Notes) {
		notes = ParseNotes(tokenizer);
		token = tokenizer.PeekNextToken();
	}
	
	AstNodeCodeBlock* template_code_block = nullptr;
	if (token.keyword == KeywordType::Template) {
		template_code_block = ParseTemplate(tokenizer);
		token = tokenizer.PeekNextToken();
	}
	
	auto* declaration = AstNew<AstNodeDeclaration>(tokenizer.alloc);
	
	if (token.keyword == KeywordType::Struct) {
		auto* ast_node_struct = ParseStruct(tokenizer, notes);
		ast_node_struct->template_code_block = template_code_block;
		
		tokenizer.ExpectToken(TokenType::Semicolon);
		
		declaration->name             = ast_node_struct->name;
		declaration->type_declaration = ast_node_struct;
		declaration->declaration_type = AstNodeDeclarationType::Typename;
	} else if (token.keyword == KeywordType::CompileConst || token.type == TokenType::Identifier) {
		bool is_constant = token.keyword == KeywordType::CompileConst;
		if (is_constant) tokenizer.FindNextToken();
		
		ParseIdentifierWithNamespace(tokenizer);
		
		token = tokenizer.PeekNextToken();
		if (token.type == TokenType::Less) {
			SkipTokensWithNestingTracking(tokenizer, TokenType::Less, TokenType::Greater); // Skip over template parameters.
		}
		
		declaration->name = tokenizer.ExpectToken(TokenType::Identifier).string;
		declaration->declaration_type = is_constant ? AstNodeDeclarationType::Constant : AstNodeDeclarationType::Variable;
		
		token = tokenizer.PeekNextToken();
		if (token.type == TokenType::Assign) {
			SkipTokens(tokenizer, TokenType::Assign, TokenType::Semicolon); // Skip variable initialization.
		} else {
			tokenizer.ExpectToken(TokenType::Semicolon);
		}
	} else {
		tokenizer.ReportError(token, "Unexpected token in a declaration.");
	}
	
	return declaration;
}

static AstNodeCodeBlock* ParseFile(StackAllocator* alloc, String file, String filepath) {
	Tokenizer tokenizer;
	tokenizer.file     = file;
	tokenizer.filepath = filepath;
	tokenizer.string   = file.data;
	tokenizer.alloc    = alloc;
	
	auto* code_block = AstNew<AstNodeCodeBlock>(alloc);
	Array<AstNodeDeclaration*> declarations;
	
	u32 brace_nesting_depth = 0;
	Array<u32> namespace_brace_depth_stack;
	
	auto token = tokenizer.PeekNextToken();
	while (token.type != TokenType::None) {
		
		if (token.type == TokenType::OpeningBrace) {
			brace_nesting_depth += 1;
		} else if (token.type == TokenType::ClosingBrace) {
			brace_nesting_depth -= 1;
			
			while (namespace_brace_depth_stack.count != 0 && ArrayLastElement(namespace_brace_depth_stack) >= brace_nesting_depth) {
				ArrayPopLast(namespace_brace_depth_stack);
				ArrayPopLast(tokenizer.namespace_stack);
			}
		}
		
		if (token.keyword == KeywordType::Notes) {
			auto* declaration = ParseDeclaration(tokenizer);
			ArrayAppend(declarations, tokenizer.alloc, declaration);
		} else if (token.keyword == KeywordType::Namespace) {
			tokenizer.FindNextToken();
			auto name = ParseIdentifierWithNamespace(tokenizer);
			
			token = tokenizer.PeekNextToken();
			
			if (token.type == TokenType::OpeningBrace) {
				ArrayAppend(namespace_brace_depth_stack, alloc, brace_nesting_depth);
				ArrayAppend(tokenizer.namespace_stack, alloc, name.string);
			}
		} else {
			tokenizer.FindNextToken(); // Skip over unknown tokens.
		}
		token = tokenizer.PeekNextToken();
	}
	code_block->declarations = declarations;
	
	return code_block;
}

static void GenerateCodeForCodeBlockTypeDeclarations(StringBuilder& builder, AstNodeCodeBlock* code_block);

static String TemplateExpressionArguments(StackAllocator* alloc, AstNodeCodeBlock* template_code_block) {
	if (template_code_block->declarations.count == 0) return ""_sl;
	if (template_code_block->declarations.count == 1) return template_code_block->declarations[0]->name;
	
	Array<String> arguments;
	ArrayReserve(arguments, alloc, template_code_block->declarations.count);
	
	for (auto* declaration : template_code_block->declarations) {
		ArrayAppend(arguments, declaration->name);
	}
	
	return StringJoin(alloc, arguments, ", "_sl);
}

static void GenerateCodeForStruct(StringBuilder& builder, AstNodeStruct* ast_node_struct) {
	// Generate code for internal structs first.
	auto* code_block = ast_node_struct->code_block;
	GenerateCodeForCodeBlockTypeDeclarations(builder, code_block);
	
	auto name = code_block->namespace_path;
	auto* template_code_block = ast_node_struct->template_code_block;
	
	if (template_code_block) {
		auto template_expression = template_code_block->declared_namespace;
		auto template_arguments = TemplateExpressionArguments(builder.alloc, template_code_block);
		name = StringFormat(builder.alloc, "%.*s<%.*s>", (s32)name.count, name.data, (s32)template_arguments.count, template_arguments.data);;
		
		builder.Append("template<%.*s> struct TypeInfoOfInternal<const %.*s> { static TypeInfoStruct* Get(); };\n\n",
			(s32)template_expression.count, template_expression.data,
			(s32)name.count, name.data
		);
		
		builder.Append("template<%.*s> TypeInfoStruct* TypeInfoOfInternal<const %.*s>::Get() {\n",
			(s32)template_expression.count, template_expression.data,
			(s32)name.count, name.data
		);
	} else {
		builder.Append("template<> struct TypeInfoOfInternal<const %.*s> { static TypeInfoStruct* Get(); };\n\n", (s32)name.count, name.data);
		
		builder.Append("TypeInfoStruct* TypeInfoOfInternal<const %.*s>::Get() {\n", (s32)name.count, name.data);
	}
	builder.Indent();
	
	// Rename the type so it's possible to use offsetof macro with types that have comma in the name.
	builder.Append("using TypeName = %.*s;\n\n", (s32)name.count, name.data);
	
	// Array of notes.
	auto* notes = ast_node_struct->notes;
	if (notes && notes->notes.count != 0) {
		u32 note_count = 0;
		for (auto& note : notes->notes) {
			builder.Append("static auto note_%u = %.*s;\n", note_count, (s32)note.expression.count, note.expression.data);
			note_count += 1;
		}
		builder.AppendUnformatted("\n"_sl);
		
		builder.Append("static TypeInfoNote notes[] = {\n");
		builder.Indent();
		
		note_count = 0;
		for (auto& note : notes->notes) {
			builder.Append("{ TypeInfoOf<decltype(note_%u)>(), &note_%u },\n", note_count, note_count);
			note_count += 1;
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	// Array of fields.
	u32 field_count = 0;
	if (code_block->declarations.count != 0 || (template_code_block && template_code_block->declarations.count != 0)) {
		builder.AppendUnformatted("static TypeInfoStructField fields[] = {\n"_sl);
		builder.Indent();
		
		if (template_code_block) {
			for (auto* declaration : template_code_block->declarations) {
				builder.Append("{ \"%.*s\"_sl, &type_info_type, 0, TypeInfoOf<%.*s>(), TypeInfoStructFieldFlags::TemplateParameter },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)declaration->name.count, declaration->name.data
				);
			}
			field_count += (u32)template_code_block->declarations.count;
		}
		
		for (auto* declaration : code_block->declarations) {
			if (declaration->declaration_type == AstNodeDeclarationType::Variable) {
				builder.Append("{ \"%.*s\"_sl, TypeInfoOf<decltype(TypeName::%.*s)>(), offsetof(TypeName, %.*s) },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)declaration->name.count, declaration->name.data,
					(s32)declaration->name.count, declaration->name.data
				);
			} else if (declaration->declaration_type == AstNodeDeclarationType::Constant) {
				builder.Append("{ \"%.*s\"_sl, TypeInfoOf<decltype(TypeName::%.*s)>(), 0, &TypeName::%.*s },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)declaration->name.count, declaration->name.data,
					(s32)declaration->name.count, declaration->name.data
				);
			} else if (declaration->declaration_type == AstNodeDeclarationType::Typename) {
				builder.Append("{ \"%.*s\"_sl, &type_info_type, 0, TypeInfoOf<TypeName::%.*s>() },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)declaration->name.count, declaration->name.data
				);
			} else {
				DebugAssertAlways("Unhanlded declaration type.");
			}
		}
		field_count += (u32)code_block->declarations.count;
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	// TypeInfoStruct:
	{
		builder.AppendUnformatted("static TypeInfoStruct type_info = {\n"_sl);
		builder.Indent();
		
		builder.Append("TypeInfoType::Struct,\n");
		builder.Append("\"%.*s\"_sl,\n", (s32)name.count, name.data);
		builder.AppendUnformatted("sizeof(TypeName),\n"_sl);
		
		if (field_count != 0) {
			builder.Append("ArrayView<TypeInfoStructField>{ fields, %u },\n", field_count);
		} else {
			builder.AppendUnformatted("ArrayView<TypeInfoStructField>{},\n"_sl);
		}
		
		if (notes && notes->notes.count != 0) {
			builder.Append("ArrayView<TypeInfoNote>{ notes, %u },\n", notes->notes.count);
		} else {
			builder.AppendUnformatted("ArrayView<TypeInfoNote>{},\n"_sl);
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	builder.AppendUnformatted("return &type_info;\n"_sl);
	builder.Unindent();
	builder.AppendUnformatted("}\n\n"_sl);
}

static void GenerateCodeForCodeBlockTypeDeclarations(StringBuilder& builder, AstNodeCodeBlock* code_block) {
	for (auto* declaration : code_block->declarations) {
		if (declaration->type_declaration == nullptr) continue;
		
		switch (declaration->type_declaration->type) {
		case AstNodeType::Struct: {
			GenerateCodeForStruct(builder, (AstNodeStruct*)declaration->type_declaration);
			break;
		} default: {
			DebugAssertAlways("Unhanlded type declaration ast node type.");
			break;
		}
		}
	}
}

static void GenerateCodeForTypeTable(StringBuilder& builder, AstNodeCodeBlock* code_block) {
	builder.AppendUnformatted("TypeInfo* type_table_internal[] = {\n"_sl);
	builder.Indent();
	
	u32 type_table_size = 0;
	for (auto* declaration : code_block->declarations) {
		if (declaration->type_declaration == nullptr) continue;
		
		String name;
		switch (declaration->type_declaration->type) {
		case AstNodeType::Struct: {
			auto* ast_node_struct = (AstNodeStruct*)declaration->type_declaration;
			name = ast_node_struct->template_code_block ? ""_sl : ast_node_struct->code_block->namespace_path;
			break;
		} default: {
			DebugAssertAlways("Unhanlded type declaration ast node type.");
			break;
		}
		}
		
		if (name.count != 0) {
			builder.Append("TypeInfoOf<%.*s>(),\n", (s32)name.count, name.data);
			type_table_size += 1;
		}
	}
	
	builder.Unindent();
	builder.AppendUnformatted("};\n\n"_sl);
	
	builder.Append("ArrayView<TypeInfo*> type_table = { type_table_internal, %u };\n\n", type_table_size);
}

#include <stdio.h>

s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	auto filepath = "./Engine/RenderPasses.h"_sl;
	auto file = SystemReadFileToString(&alloc, filepath);
	if (file.data == nullptr) {
		SystemWriteToConsole(&alloc, "Failed to open file '%s'.\n", filepath.data);
		return 1;
	}
	
	auto* top_level_code_block = ParseFile(&alloc, file, filepath);
	
	StringBuilder builder;
	builder.alloc = &alloc;
	
	builder.Append("#include \"Engine/RenderPasses.h\"\n");
	builder.Append("#include \"Metaprogram/TypeInfo.h\"\n");
	builder.Append("#include <stddef.h>\n\n"); // Included to get offsetof().
	
	GenerateCodeForCodeBlockTypeDeclarations(builder, top_level_code_block);
	GenerateCodeForTypeTable(builder, top_level_code_block);
	
	
	auto output_directory = "./Metaprogram/Generated/"_sl;
	if (SystemCreateDirectory(&alloc, output_directory) == false) {
		SystemWriteToConsole(&alloc, "Failed to create output directory '%s'.\n", output_directory.data);
		return 1;
	}
	
	auto output_filepath = "./Metaprogram/Generated/RenderPasses.cpp"_sl;
	auto output_file = SystemOpenFile(&alloc, output_filepath, OpenFileFlags::Write);
	if (output_file.handle == nullptr) {
		SystemWriteToConsole(&alloc, "Failed to open output file '%s'.\n", output_filepath.data);
		return 1;
	}
	
	auto file_string = builder.ToString();
	SystemWriteFile(output_file, file_string.data, file_string.count, 0);
	SystemCloseFile(output_file);
	
	return 0;
}

