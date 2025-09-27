#include "Tokens.h"

static bool IsLineEnding(char c) { return (c == '\n') || (c == '\r'); }
static bool IsWhiteSpace(char c) { return (c == ' ') || (c == '\t') || IsLineEnding(c); }
static bool IsNumeric(char c)    { return c >= '0' && c <= '9'; }
static bool IsAlphabetical(char c) { return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (c == '_'); }
static bool IsAlphaNumeric(char c) { return IsAlphabetical(c) || IsNumeric(c); }

static const char* EatWhiteSpace(const char* string) {
	while (*string && IsWhiteSpace(*string)) string += 1;
	return string;
}

static const char* EatSingleLineComment(const char* string) {
	while (*string && IsLineEnding(*string) == false) string += 1;
	while (IsLineEnding(*string)) string += 1;
	return string;
}

static const char* EatMultiLineComment(const char* string) {
	while (*string && !(string[0] == '*' && string[1] == '/')) string += 1;
	if (string[0] == '*' && string[1] == '/') string += 2;
	return string;
}

static const char* EatWhiteSpaceAndComments(const char* string) {
	string = EatWhiteSpace(string);
	while (string[0] == '/') {
		if (string[1] == '/') {
			string = EatSingleLineComment(string + 2);
			string = EatWhiteSpace(string);
		} else if (string[1] == '*') {
			string = EatMultiLineComment(string + 2);
			string = EatWhiteSpace(string);
		} else {
			break;
		}
	}
	return string;
}

static Token ParseStringLiteral(const char*& string) {
	string += 1;
	
	Token token;
	token.string.data = (char*)string;
	token.type = TokenType::String;
	
	u32 backslash_count = 0;
	while (*string && !(string[0] == '"' && (backslash_count & 0x1) == 0)) {
		if (string[0] == '\\') {
			backslash_count += 1;
		} else {
			backslash_count = 0;
		}
		string += 1;
	}
	
	token.string.count = (string - token.string.data);
	string += 1;
	
	return token;
}

static Token MakeToken(const char*& string, u32 length, TokenType type) {
	Token token;
	token.type   = type;
	token.string.data  = (char*)string;
	token.string.count = length;
	
	string += length;
	
	return token;
}

static bool CheckKeyword(Token& token, const char* name, u32 length, KeywordType keyword) {
	if (memcmp(token.string.data, name, length) == 0) {
		token.type    = TokenType::Keyword;
		token.keyword = keyword;
		return true;
	}
	
	return false;
}

Token Tokenizer::FindNextToken() {
	string = EatWhiteSpaceAndComments(string);
	
	char leading_char = *string;
	switch (leading_char) {
	case '+': return MakeToken(string, 1, TokenType::Plus);
	case '-': return MakeToken(string, 1, TokenType::Minus);
	case '*': return MakeToken(string, 1, TokenType::Times);
	case '/': return MakeToken(string, 1, TokenType::Divide);
	case '%': return MakeToken(string, 1, TokenType::Modulo);
	case '<': return MakeToken(string, 1, TokenType::Less);
	case '>': return MakeToken(string, 1, TokenType::Greater);
	case '^': return MakeToken(string, 1, TokenType::Xor);
	case '&': return MakeToken(string, 1, TokenType::BitwiseAnd);
	case '|': return MakeToken(string, 1, TokenType::BitwiseOr);
	case '~': return MakeToken(string, 1, TokenType::BitwiseNot);
	case '!': return MakeToken(string, 1, TokenType::LogicalNot);
	case '=': return MakeToken(string, 1, TokenType::Assign);
	case '(': return MakeToken(string, 1, TokenType::OpeningParen);
	case ')': return MakeToken(string, 1, TokenType::ClosingParen);
	case '{': return MakeToken(string, 1, TokenType::OpeningBrace);
	case '}': return MakeToken(string, 1, TokenType::ClosingBrace);
	case '[': return MakeToken(string, 1, TokenType::OpeningBracket);
	case ']': return MakeToken(string, 1, TokenType::ClosingBracket);
	case ',': return MakeToken(string, 1, TokenType::Comma);
	case '.': return MakeToken(string, 1, TokenType::Dot);
	case ':': return MakeToken(string, 1, TokenType::Colon);
	case ';': return MakeToken(string, 1, TokenType::Semicolon);
	case '#': return MakeToken(string, 1, TokenType::Hash);
	case '"': return ParseStringLiteral(string);
	}
	
	Token token;
	if (IsAlphaNumeric(leading_char)) {
		token.string.data = (char*)string;
		
		if (IsNumeric(leading_char)) {
			token.type = TokenType::Number;
			while (IsAlphaNumeric(*string) || *string == '.') string += 1;
		} else {
			token.type = TokenType::Identifier;
			while (IsAlphaNumeric(*string)) string += 1;
		}
		
		token.string.count = (string - token.string.data);
	}
	
	if (token.type == TokenType::Identifier) {
		switch (token.string.count) {
		case 4: {
			if (CheckKeyword(token, "enum", 4, KeywordType::Enum)) break;
			break;
		} case 5: {
			if (CheckKeyword(token, "union", 5, KeywordType::Union)) break;
			if (CheckKeyword(token, "NOTES", 5, KeywordType::Notes)) break;
			break;
		} case 6: {
			if (CheckKeyword(token, "struct", 6, KeywordType::Struct)) break;
			break;
		} case 8: {
			if (CheckKeyword(token, "template", 8, KeywordType::Template)) break;
			break;
		} case 9: {
			if (CheckKeyword(token, "namespace", 8, KeywordType::Namespace)) break;
			break;
		}
		}
	}
	
	return token;
}

Token Tokenizer::PeekNextToken() {
	const char* backup_string = string;
	defer{ string = backup_string; };
	
	return FindNextToken();
}


String token_type_names[] = {
	"None"_sl,
	"Plus"_sl,
	"Minus"_sl,
	"Times"_sl,
	"Divide"_sl,
	"Modulo"_sl,
	"Xor"_sl,
	"BitwiseAnd"_sl,
	"BitwiseOr"_sl,
	"BitwiseNot"_sl,
	"LogicalNot"_sl,
	"Less"_sl,
	"Greater"_sl,
	"OpeningParen"_sl,
	"ClosingParen"_sl,
	"OpeningBrace"_sl,
	"ClosingBrace"_sl,
	"OpeningBracket"_sl,
	"ClosingBracket"_sl,
	"Comma"_sl,
	"Dot"_sl,
	"Assign"_sl,
	"Colon"_sl,
	"Semicolon"_sl,
	"Hash"_sl,
	"Number"_sl,
	"Identifier"_sl,
	"Keyword"_sl,
	"String"_sl,
};
static_assert(ArraySize(token_type_names) == (u32)TokenType::Count, "Mismatching token_type_name count.");

String keyword_type_names[] = {
	"None"_sl,
	"Notes"_sl,
	"Enum"_sl,
	"Struct"_sl,
	"Union"_sl,
	"Template"_sl,
	"Namespace"_sl,
};
static_assert(ArraySize(keyword_type_names) == (u32)KeywordType::Count, "Mismatching keyword_type_names count.");

