#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Tokens.h"
#include "AstNodes.h"
#include "MetaprogramCommon.h"


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
		tokenizer.ReportError(token, "Unexpected end of file in a list."_sl);
	}
	
	return token;
}

static Token SkipTokens(Tokenizer& tokenizer, TokenType opening_token, TokenType closing_token) {
	auto token = tokenizer.ExpectToken(opening_token);
	
	while (token.type != TokenType::None && token.type != closing_token) {
		token = tokenizer.FindNextToken();
	}
	
	if (token.type != closing_token) {
		tokenizer.ReportError(token, "Unexpected end of file in a list."_sl);
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
			tokenizer.ReportError(token, "Unexpected token. Expected 'Comma' or 'ClosingParen'."_sl);
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
	code_block->template_expression.data  = token_0.string.data;
	code_block->template_expression.count = token_1.string.data - token_0.string.data;
	
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
	code_block->namespace_path = NamespaceStackToString(tokenizer);
	
	Array<AstNodeDeclaration*> declarations;
	while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
		auto* declaration = ParseDeclaration(tokenizer);
		if (declaration) ArrayAppend(declarations, tokenizer.alloc, declaration);
		
		token = tokenizer.PeekNextToken();
	}
	code_block->declarations = declarations;
	
	tokenizer.ExpectToken(TokenType::ClosingBrace);
	
	auto* ast_node_struct = AstNew<AstNodeStruct>(tokenizer.alloc);
	ast_node_struct->name       = name.string;
	ast_node_struct->code_block = code_block;
	ast_node_struct->notes      = notes;
	
	return ast_node_struct;
}

static AstNodeEnum* ParseEnum(Tokenizer& tokenizer, AstNodeNotes* notes) {
	tokenizer.ExpectKeyword(KeywordType::Enum);
	tokenizer.ExpectKeyword(KeywordType::Struct);
	
	auto name = tokenizer.ExpectToken(TokenType::Identifier);
	ArrayAppend(tokenizer.namespace_stack, tokenizer.alloc, name.string);
	defer{ ArrayPopLast(tokenizer.namespace_stack); };
	
	auto token = tokenizer.PeekNextToken();
	
	String underlying_type;
	if (token.type == TokenType::Colon) {
		tokenizer.FindNextToken();
		underlying_type = ParseIdentifierWithNamespace(tokenizer).string;
	}
	
	tokenizer.ExpectToken(TokenType::OpeningBrace);
	
	token = tokenizer.PeekNextToken();
	
	auto* code_block = AstNew<AstNodeCodeBlock>(tokenizer.alloc);
	code_block->namespace_path = NamespaceStackToString(tokenizer);
	
	Array<AstNodeDeclaration*> declarations;
	while (token.type != TokenType::None && token.type != TokenType::ClosingBrace) {
		auto* declaration = AstNew<AstNodeDeclaration>(tokenizer.alloc);
		declaration->name = tokenizer.ExpectToken(TokenType::Identifier).string;
		declaration->declaration_type = AstNodeDeclarationType::Constant;
		ArrayAppend(declarations, tokenizer.alloc, declaration);
		
		token = tokenizer.PeekNextToken();
		if (token.type == TokenType::Assign) {
			tokenizer.FindNextToken();
			
			token = tokenizer.PeekNextToken();
			while (token.type != TokenType::None && token.type != TokenType::Comma && token.type != TokenType::ClosingBrace) {
				tokenizer.FindNextToken();
			
				token = tokenizer.PeekNextToken();
			}
		} else {
			token = tokenizer.PeekNextToken();
		}
		
		if (token.type != TokenType::ClosingBrace) {
			tokenizer.ExpectToken(TokenType::Comma);
			token = tokenizer.PeekNextToken();
		}
	}
	code_block->declarations = declarations;
	
	tokenizer.ExpectToken(TokenType::ClosingBrace);
	
	auto* ast_node_enum = AstNew<AstNodeEnum>(tokenizer.alloc);
	ast_node_enum->name            = name.string;
	ast_node_enum->underlying_type = underlying_type;
	ast_node_enum->code_block      = code_block;
	ast_node_enum->notes           = notes;
	
	return ast_node_enum;
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
	
	if (token.keyword == KeywordType::Inline) {
		tokenizer.FindNextToken();
		token = tokenizer.PeekNextToken();
	}
	
	bool is_static = false;
	if (token.keyword == KeywordType::Static) {
		is_static = true;
		tokenizer.FindNextToken();
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
	} else if (token.keyword == KeywordType::Enum) {
		auto* ast_node_enum = ParseEnum(tokenizer, notes);
		tokenizer.ExpectToken(TokenType::Semicolon);
		
		declaration->name             = ast_node_enum->name;
		declaration->type_declaration = ast_node_enum;
		declaration->declaration_type = AstNodeDeclarationType::Typename;
	} else if (token.keyword == KeywordType::CompileConst || token.type == TokenType::Identifier) {
		bool is_constant = token.keyword == KeywordType::CompileConst;
		if (is_constant) tokenizer.FindNextToken();
		
		ParseIdentifierWithNamespace(tokenizer);
		
		token = tokenizer.PeekNextToken();
		
		// Skip over template parameters.
		if (token.type == TokenType::Less) {
			SkipTokensWithNestingTracking(tokenizer, TokenType::Less, TokenType::Greater);
			token = tokenizer.PeekNextToken();
		}
		
		// Skip over pointers and references.
		while (token.type != TokenType::None && (token.type == TokenType::Times || token.type == TokenType::BitwiseAnd)) {
			token = tokenizer.FindNextToken();
			token = tokenizer.PeekNextToken();
		}
		
		// Skip over operators.
		if (token.type == TokenType::Keyword && token.keyword == KeywordType::Operator) {
			tokenizer.ExpectKeyword(KeywordType::Operator);
			
			token = tokenizer.FindNextToken();
			if (token.type == TokenType::OpeningBracket) {
				tokenizer.ExpectToken(TokenType::ClosingBracket);
			}
			
			token = tokenizer.PeekNextToken();
			if (token.type != TokenType::OpeningParen) {
				tokenizer.ReportError(token, "Expected 'OpeningParen' after operator."_sl);
			}
		}
		
		// Extract declaration name.
		if (token.type != TokenType::OpeningParen) {
			declaration->name = tokenizer.ExpectToken(TokenType::Identifier).string;
			declaration->declaration_type = is_constant ? AstNodeDeclarationType::Constant : AstNodeDeclarationType::Variable;
		}
		
		token = tokenizer.PeekNextToken();
		if (token.type == TokenType::Assign) { // Skip variable initialization.
			SkipTokens(tokenizer, TokenType::Assign, TokenType::Semicolon);
		} else if (token.type == TokenType::OpeningParen) { // Skip function.
			declaration = nullptr;
			SkipTokensWithNestingTracking(tokenizer, TokenType::OpeningParen, TokenType::ClosingParen);
			
			token = tokenizer.PeekNextToken();
			if (token.type == TokenType::OpeningBrace) {
				SkipTokensWithNestingTracking(tokenizer, TokenType::OpeningBrace, TokenType::ClosingBrace);
			} else {
				tokenizer.ExpectToken(TokenType::Semicolon);
			}
		} else {
			tokenizer.ExpectToken(TokenType::Semicolon);
		}
	} else {
		tokenizer.ReportError(token, "Unexpected token in a declaration."_sl);
	}
	
	// Skip static declarations for now.
	return is_static ? nullptr : declaration;
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
			if (declaration == nullptr) {
				tokenizer.ReportError(token, "Expected declaration after notes."_sl);
			}
			
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

static void GenerateCodeForCodeBlockTypeDeclarations(StringBuilder& builder, AstNodeCodeBlock* code_block, bool forward_declaration);

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

static void GenerateCodeForNotes(StringBuilder& builder, AstNodeNotes* notes) {
	if (notes == nullptr || notes->notes.count == 0) return;
	
	u32 note_count = 0;
	for (auto& note : notes->notes) {
		builder.Append("static auto note_% = %;\n"_sl, note_count, note.expression);
		note_count += 1;
	}
	builder.Append("\n"_sl);
	
	builder.Append("static TypeInfoNote notes[] = {\n"_sl);
	builder.Indent();
	
	note_count = 0;
	for (auto& note : notes->notes) {
		builder.Append("{ TypeInfoOf<decltype(note_%0)>(), &note_%0 },\n"_sl, note_count);
		note_count += 1;
	}
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
}

static void GenerateCodeForStruct(StringBuilder& builder, AstNodeStruct* ast_node_struct, bool forward_declaration) {
	// Generate code for internal structs first.
	auto* code_block = ast_node_struct->code_block;
	GenerateCodeForCodeBlockTypeDeclarations(builder, code_block, forward_declaration);
	
	auto name = code_block->namespace_path;
	auto* template_code_block = ast_node_struct->template_code_block;
	
	if (template_code_block) {
		auto template_expression = template_code_block->template_expression;
		auto template_arguments = TemplateExpressionArguments(builder.alloc, template_code_block);
		name = StringFormat(builder.alloc, "%<%>"_sl, name, template_arguments);
		
		if (forward_declaration) {
			builder.Append("template<%> struct TypeInfoOfInternal<const %> { static TypeInfoStruct* Get(); };\n"_sl, template_expression, name);
		} else {
			builder.Append("template<%> TypeInfoStruct* TypeInfoOfInternal<const %>::Get() {\n"_sl, template_expression, name);
		}
	} else {
		if (forward_declaration) {
			builder.Append("template<> struct TypeInfoOfInternal<const %> { static TypeInfoStruct* Get(); };\n"_sl, name);
		} else {
			builder.Append("TypeInfoStruct* TypeInfoOfInternal<const %>::Get() {\n"_sl, name);
		}
	}
	if (forward_declaration) return;
	
	builder.Indent();
	
	// Rename the type so it's possible to use offsetof macro with types that have comma in the name.
	builder.Append("using TypeName = %;\n\n"_sl, name);
	
	// Array of notes.
	auto* notes = ast_node_struct->notes;
	GenerateCodeForNotes(builder, notes);
	
	// Array of fields.
	u32 field_count = 0;
	if (code_block->declarations.count != 0 || (template_code_block && template_code_block->declarations.count != 0)) {
		builder.Append("static TypeInfoStructField fields[] = {\n"_sl);
		builder.Indent();
		
		if (template_code_block) {
			for (auto* declaration : template_code_block->declarations) {
				builder.Append("{ \"%0\"_sl, &type_info_type, 0, TypeInfoOf<%0>(), TypeInfoStructFieldFlags::TemplateParameter },\n"_sl, declaration->name);
			}
			field_count += (u32)template_code_block->declarations.count;
		}
		
		for (auto* declaration : code_block->declarations) {
			if (declaration->declaration_type == AstNodeDeclarationType::Variable) {
				builder.Append("{ \"%0\"_sl, TypeInfoOf<decltype(TypeName::%0)>(), offsetof(TypeName, %0) },\n"_sl, declaration->name);
			} else if (declaration->declaration_type == AstNodeDeclarationType::Constant) {
				builder.Append("{ \"%0\"_sl, TypeInfoOf<decltype(TypeName::%0)>(), 0, &TypeName::%0 },\n"_sl, declaration->name);
			} else if (declaration->declaration_type == AstNodeDeclarationType::Typename) {
				builder.Append("{ \"%0\"_sl, &type_info_type, 0, TypeInfoOf<TypeName::%0>() },\n"_sl, declaration->name);
			} else {
				DebugAssertAlways("Unhanlded declaration type.");
			}
		}
		field_count += (u32)code_block->declarations.count;
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	// TypeInfoStruct:
	{
		builder.Append("static TypeInfoStruct type_info = {\n"_sl);
		builder.Indent();
		
		builder.Append("TypeInfoType::Struct,\n"_sl);
		builder.Append("\"%0\"_sl,\n"_sl, name);
		builder.Append("sizeof(TypeName),\n"_sl);
		
		if (field_count != 0) {
			builder.Append("ArrayView<TypeInfoStructField>{ fields, % },\n"_sl, field_count);
		} else {
			builder.Append("ArrayView<TypeInfoStructField>{},\n"_sl);
		}
		
		if (notes && notes->notes.count != 0) {
			builder.Append("ArrayView<TypeInfoNote>{ notes, % },\n"_sl, notes->notes.count);
		} else {
			builder.Append("ArrayView<TypeInfoNote>{},\n"_sl);
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	builder.Append("return &type_info;\n"_sl);
	builder.Unindent();
	builder.Append("}\n\n"_sl);
}

static void GenerateCodeForEnum(StringBuilder& builder, AstNodeEnum* ast_node_enum, bool forward_declaration) {
	auto* code_block = ast_node_enum->code_block;
	auto name = code_block->namespace_path;
	
	if (forward_declaration) {
		builder.Append("template<> struct TypeInfoOfInternal<const %> { static TypeInfoEnum* Get(); };\n"_sl, name);
		return;
	}
	
	builder.Append("TypeInfoEnum* TypeInfoOfInternal<const %>::Get() {\n"_sl, name);
	builder.Indent();
	
	builder.Append("using TypeName = %;\n\n"_sl, name);
	
	// Array of notes.
	auto* notes = ast_node_enum->notes;
	GenerateCodeForNotes(builder, notes);
	
	// Array of fields.
	if (code_block->declarations.count != 0) {
		builder.Append("static TypeInfoEnumField fields[] = {\n"_sl);
		builder.Indent();
		
		for (auto* declaration : code_block->declarations) {
			builder.Append("{ \"%0\"_sl, (u64)TypeName::%0 },\n"_sl, declaration->name);
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	// TypeInfoEnum:
	{
		builder.Append("static TypeInfoEnum type_info = {\n"_sl);
		builder.Indent();
		
		builder.Append("TypeInfoType::Enum,\n"_sl);
		builder.Append("\"%\"_sl,\n"_sl, name);
		
		auto underlying_type = ast_node_enum->underlying_type.count ? ast_node_enum->underlying_type : "s32"_sl;
		builder.Append("TypeInfoOf<%>(),\n"_sl, underlying_type);
		
		if (code_block->declarations.count != 0) {
			builder.Append("ArrayView<TypeInfoEnumField>{ fields, % },\n"_sl, code_block->declarations.count);
		} else {
			builder.Append("ArrayView<TypeInfoEnumField>{},\n"_sl);
		}
		
		if (notes && notes->notes.count != 0) {
			builder.Append("ArrayView<TypeInfoNote>{ notes, % },\n"_sl, notes->notes.count);
		} else {
			builder.Append("ArrayView<TypeInfoNote>{},\n"_sl);
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	builder.Append("return &type_info;\n"_sl);
	builder.Unindent();
	builder.Append("};\n\n"_sl);
}

static void GenerateCodeForCodeBlockTypeDeclarations(StringBuilder& builder, AstNodeCodeBlock* code_block, bool forward_declaration) {
	for (auto* declaration : code_block->declarations) {
		if (declaration->type_declaration == nullptr) continue;
		
		switch (declaration->type_declaration->type) {
		case AstNodeType::Struct: {
			GenerateCodeForStruct(builder, (AstNodeStruct*)declaration->type_declaration, forward_declaration);
			break;
		} case AstNodeType::Enum: {
			GenerateCodeForEnum(builder, (AstNodeEnum*)declaration->type_declaration, forward_declaration);
			break;
		} default: {
			DebugAssertAlways("Unhanlded type declaration ast node type.");
			break;
		}
		}
	}
}

struct ParsedFileData {
	String filepath;
	AstNodeCodeBlock* top_level_code_block = nullptr;
};

static void GenerateCodeForTypeTable(StringBuilder& builder, ArrayView<ParsedFileData> files) {
	builder.Append("static TypeInfo* type_table_internal[] = {\n"_sl);
	builder.Indent();
	
	u32 type_table_size = 0;
	for (auto& file : files) {
		for (auto* declaration : file.top_level_code_block->declarations) {
			if (declaration->type_declaration == nullptr) continue;
			
			String name;
			switch (declaration->type_declaration->type) {
			case AstNodeType::Struct: {
				auto* ast_node_struct = (AstNodeStruct*)declaration->type_declaration;
				name = ast_node_struct->template_code_block ? ""_sl : ast_node_struct->code_block->namespace_path;
				break;
			} case AstNodeType::Enum: {
				auto* ast_node_enum = (AstNodeEnum*)declaration->type_declaration;
				name = ast_node_enum->code_block->namespace_path;
				break;
			} default: {
				DebugAssertAlways("Unhanlded type declaration ast node type.");
				break;
			}
			}
			
			if (name.count != 0) {
				builder.Append("TypeInfoOf<%>(),\n"_sl, name);
				type_table_size += 1;
			}
		}
	}
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	builder.Append("ArrayView<TypeInfo*> type_table = { type_table_internal, % };\n\n"_sl, type_table_size);
}


s32 main(s32 argument_count, const char* arguments[]) {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	Array<String> filepaths;
	ArrayReserve(filepaths, &alloc, argument_count - 1);
	
	for (s32 i = 1; i < argument_count; i += 1) {
		ArrayAppend(filepaths, StringFromCString(arguments[i]));
	}
	
	
	Array<ParsedFileData> parsed_files;
	for (auto filepath : filepaths) {
		auto file = SystemReadFileToString(&alloc, filepath);
		if (file.data == nullptr) {
			SystemWriteToConsole(&alloc, "Failed to open file '%'.\n"_sl, filepath);
			SystemExitProcess(1);
		}
		
		SystemWriteToConsole(&alloc, "Parsing file: '%'.\n"_sl, filepath);
		
		ParsedFileData ast_node_file;
		ast_node_file.filepath = filepath;
		ast_node_file.top_level_code_block = ParseFile(&alloc, file, filepath);
		ArrayAppend(parsed_files, &alloc, ast_node_file);
	}
	
	
	EnsureDirectoryExists(&alloc, "Metaprogram/Generated/"_sl);
	
	{
		StringBuilder builder;
		builder.alloc = &alloc;
		
		for (auto& file : parsed_files) {
			builder.Append("#include \"%\"\n"_sl, file.filepath);
		}
		builder.Append("#include \"Metaprogram/TypeInfo.h\"\n"_sl);
		builder.Append("#include <stddef.h>\n\n"_sl); // Included to get offsetof().
		
		builder.Append("// Forward Declarations:\n"_sl);
		for (auto& file : parsed_files) {
			GenerateCodeForCodeBlockTypeDeclarations(builder, file.top_level_code_block, true);
		}
		builder.Append("\n\n"_sl);
		
		builder.Append("// Type Information:\n"_sl);
		for (auto& file : parsed_files) {
			GenerateCodeForCodeBlockTypeDeclarations(builder, file.top_level_code_block, false);
		}
		
		GenerateCodeForTypeTable(builder, parsed_files);
		
		WriteGeneratedFile(&alloc, "Metaprogram/Generated/TypeTable.cpp"_sl, builder.ToString());
	}
	
	return 0;
}

