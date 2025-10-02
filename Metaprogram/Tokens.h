#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"

enum struct TokenType : u32 {
	None           = 0,
	Plus           = 1,  // '+'
	Minus          = 2,  // '-'
	Times          = 3,  // '*'
	Divide         = 4,  // '/'
	Modulo         = 5,  // '%'
	Xor            = 6,  // '^'
	BitwiseAnd     = 7,  // '&'
	BitwiseOr      = 8, // '|'
	BitwiseNot     = 9, // '~'
	LogicalNot     = 10, // '!'
	Less           = 11, // '<'
	Greater        = 12, // '>'
	OpeningParen   = 13, // '('
	ClosingParen   = 14, // ')'
	OpeningBrace   = 15, // '{'
	ClosingBrace   = 16, // '}'
	OpeningBracket = 17, // '['
	ClosingBracket = 18, // ']'
	Comma          = 19, // ','
	Dot            = 20, // '.'
	Assign         = 21, // '='
	Colon          = 22, // ':'
	DoubleColon    = 23, // '::'
	Semicolon      = 24, // ';'
	Hash           = 25, // '#'
	Number         = 26, // '10'
	Identifier     = 27, // 'main'
	Keyword        = 28, // 'struct'
	String         = 29, // '"hello"'
	
	Count
};
extern String token_type_names[];

enum struct KeywordType : u32 {
	None         = 0,
	Notes        = 1,
	Enum         = 2,
	Union        = 3,
	Struct       = 4,
	Static       = 5,
	Inline       = 6,
	Template     = 7,
	Typename     = 8,
	Namespace    = 9,
	CompileConst = 10,
	
	Count
};
extern String keyword_type_names[];

struct Token {
	String string;
	
	TokenType   type    = TokenType::None;
	KeywordType keyword = KeywordType::None;
};

struct Tokenizer {
	const char* string = nullptr;
	
	StackAllocator* alloc = nullptr;
	String file;
	String filepath;
	Array<String> namespace_stack;
	
	Token FindNextToken();
	Token PeekNextToken();
	Token ExpectToken(TokenType expected_type);
	Token ExpectKeyword(KeywordType expected_keyword);
	
	void ReportMessage(Token token, String message);
	void ReportError(Token token, const char* format, ...);
};
