#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicHashTable.h"
#include "Tokens.h"
#include "AstNodes.h"


static String NamespaceStackToString(Tokenizer& tokenizer) {
	if (tokenizer.namespace_stack.count == 0) return ""_sl;
	if (tokenizer.namespace_stack.count == 1) return tokenizer.namespace_stack[0];
	
	u64 total_string_length = 0;
	for (auto& name : tokenizer.namespace_stack) {
		total_string_length += name.count;
	}
	total_string_length += (tokenizer.namespace_stack.count - 1) * 2;
	
	auto string = StringAllocate(tokenizer.alloc, total_string_length);
	
	u64 offset = 0;
	for (auto& name : tokenizer.namespace_stack) {
		memcpy(string.data + offset, name.data, name.count);
		offset += name.count;
		
		if (offset < string.count) {
			memset(string.data + offset, ':', 2);
			offset += 2;
		}
	}
	
	return string;
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

static AstNodeStruct* ParseStruct(Tokenizer& tokenizer, AstNodeNotes* notes) {
	tokenizer.ExpectKeyword(KeywordType::Struct);
	
	auto name = tokenizer.ExpectToken(TokenType::Identifier);
	ArrayAppend(tokenizer.namespace_stack, tokenizer.alloc, name.string);
	defer{ ArrayPopLast(tokenizer.namespace_stack); };
	
	tokenizer.ExpectToken(TokenType::OpeningBrace);
	
	auto token = tokenizer.PeekNextToken();
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
	
	auto* declaration = AstNew<AstNodeDeclaration>(tokenizer.alloc);
	
	if (token.keyword == KeywordType::Struct) {
		auto* ast_node_struct = ParseStruct(tokenizer, notes);
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
			ArrayAppend(namespace_brace_depth_stack, alloc, brace_nesting_depth);
			tokenizer.FindNextToken();
			ArrayAppend(tokenizer.namespace_stack, alloc, ParseIdentifierWithNamespace(tokenizer).string);
		} else {
			tokenizer.FindNextToken(); // Skip over unknown tokens.
		}
		token = tokenizer.PeekNextToken();
	}
	code_block->declarations = declarations;
	
	return code_block;
}

static void GenerateCodeForCodeBlockTypeDeclarations(StringBuilder& builder, AstNodeCodeBlock* code_block);

static void GenerateCodeForStruct(StringBuilder& builder, AstNodeStruct* ast_node_struct) {
	// Generate code for internal structs first.
	auto* code_block = ast_node_struct->code_block;
	GenerateCodeForCodeBlockTypeDeclarations(builder, code_block);
	
	auto name = code_block->namespace_path;
	
	builder.Append("template<> struct TypeInfoOfInternal<const %.*s> { static TypeInfoStruct* Get(); };\n\n", (s32)name.count, name.data);
	
	builder.Append("TypeInfoStruct* TypeInfoOfInternal<const %.*s>::Get() {\n", (s32)name.count, name.data);
	builder.Indent();
	
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
	if (code_block->declarations.count != 0) {
		builder.AppendUnformatted("static TypeInfoStructField fields[] = {\n"_sl);
		builder.Indent();
		
		for (auto* declaration : code_block->declarations) {
			if (declaration->declaration_type == AstNodeDeclarationType::Variable) {
				builder.Append("{ \"%.*s\"_sl, TypeInfoOf<decltype(%.*s::%.*s)>(), offsetof(%.*s, %.*s) },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)name.count, name.data, (s32)declaration->name.count, declaration->name.data,
					(s32)name.count, name.data, (s32)declaration->name.count, declaration->name.data
				);
			} else if (declaration->declaration_type == AstNodeDeclarationType::Constant) {
				builder.Append("{ \"%.*s\"_sl, TypeInfoOf<decltype(%.*s::%.*s)>(), 0, &%.*s::%.*s },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)name.count, name.data, (s32)declaration->name.count, declaration->name.data,
					(s32)name.count, name.data, (s32)declaration->name.count, declaration->name.data
				);
			} else if (declaration->declaration_type == AstNodeDeclarationType::Typename) {
				builder.Append("{ \"%.*s\"_sl, &type_info_type, 0, TypeInfoOf<%.*s::%.*s>() },\n",
					(s32)declaration->name.count, declaration->name.data,
					(s32)name.count, name.data, (s32)declaration->name.count, declaration->name.data
				);
			} else {
				DebugAssertAlways("Unhanlded declaration type.");
			}
		}
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	// TypeInfoStruct:
	{
		builder.AppendUnformatted("static TypeInfoStruct type_info = {\n"_sl);
		builder.Indent();
		
		builder.Append("TypeInfoType::Struct,\n");
		builder.Append("\"%.*s\"_sl,\n", (s32)name.count, name.data);
		builder.Append("sizeof(%.*s),\n", (s32)name.count, name.data);
		
		if (code_block->declarations.count != 0) {
			builder.Append("ArrayView<TypeInfoStructField>{ fields, %u },\n", (u32)code_block->declarations.count);
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

s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	auto filepath = "./Engine/RenderPasses.h"_sl;
	auto file = SystemReadFileToString(&alloc, filepath);
	if (file.data == nullptr) {
		SystemWriteToConsole(&alloc, "\x1B[31mFailed to open file '%s'.\x1B[0m\n", filepath.data);
		return 1;
	}
	
	auto* top_level_code_block = ParseFile(&alloc, file, filepath);
	
	StringBuilder builder;
	builder.alloc = &alloc;
	GenerateCodeForCodeBlockTypeDeclarations(builder, top_level_code_block);
	
	auto file_string = builder.ToString();
	
	return 0;
}

