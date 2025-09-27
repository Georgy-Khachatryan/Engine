#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicHashTable.h"
#include "Tokens.h"
#include "AstNodes.h"

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
	
	auto token = tokenizer.PeekNextToken();
	while (token.type != TokenType::None) {
		if (token.keyword == KeywordType::Notes) {
			auto* declaration = ParseDeclaration(tokenizer);
			ArrayAppend(declarations, tokenizer.alloc, declaration);
		} else {
			tokenizer.FindNextToken(); // Skip over unknown tokens.
		}
		token = tokenizer.PeekNextToken();
	}
	code_block->declarations = declarations;
	
	return code_block;
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
	
	return 0;
}

