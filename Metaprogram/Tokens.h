#pragma once
#include "Basic/Basic.h"
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
	Semicolon      = 23, // ';'
	Hash           = 24, // '#'
	Number         = 25, // '10'
	Identifier     = 26, // 'main'
	Keyword        = 27, // 'struct'
	String         = 28, // '"hello"'
	
	Count
};
String token_type_names[];

enum struct KeywordType : u32 {
	None      = 0,
	Notes     = 1,
	Enum      = 2,
	Struct    = 3,
	Union     = 4,
	Template  = 5,
	Namespace = 6,
	
	Count
};
String keyword_type_names[];

struct Token {
	String string;
	
	TokenType   type    = TokenType::None;
	KeywordType keyword = KeywordType::None;
};

struct Tokenizer {
	const char* string = nullptr;
	
	Token FindNextToken();
	Token PeekNextToken();
};
