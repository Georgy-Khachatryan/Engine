#include "Tokens.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"

static bool IsLineEnding(char c) { return (c == '\n') || (c == '\r'); }
static bool IsWhiteSpace(char c) { return (c == ' ') || (c == '\t') || IsLineEnding(c); }
static bool IsAlphabetical(char c) { return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (c == '_'); }
static bool IsAlphaNumeric(char c) { return IsAlphabetical(c) || CharIsNumeric(c); }

static const char* EatWhiteSpace(const char* string) {
	while (*string && (IsWhiteSpace(*string) || *string == '\\')) string += 1;
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
	case ':': return string[1] == ':' ? MakeToken(string, 2, TokenType::DoubleColon) : MakeToken(string, 1, TokenType::Colon);
	case ';': return MakeToken(string, 1, TokenType::Semicolon);
	case '#': return MakeToken(string, 1, TokenType::Hash);
	case '"': return ParseStringLiteral(string);
	}
	
	Token token;
	token.string.data = (char*)string;
	
	if (IsAlphaNumeric(leading_char)) {
		if (CharIsNumeric(leading_char)) {
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
			if (CheckKeyword(token, "static", 6, KeywordType::Static)) break;
			if (CheckKeyword(token, "inline", 6, KeywordType::Inline)) break;
			break;
		} case 7: {
			if (CheckKeyword(token, "alignas", 7, KeywordType::AlignAs)) break;
			break;
		} case 8: {
			if (CheckKeyword(token, "template", 8, KeywordType::Template)) break;
			if (CheckKeyword(token, "typename", 8, KeywordType::Typename)) break;
			if (CheckKeyword(token, "operator", 8, KeywordType::Operator)) break;
			break;
		} case 9: {
			if (CheckKeyword(token, "namespace", 8, KeywordType::Namespace)) break;
			break;
		} case 13: {
			if (CheckKeyword(token, "compile_const", 13, KeywordType::CompileConst)) break;
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

Token Tokenizer::ExpectToken(TokenType expected_type) {
	auto token = FindNextToken();
	if (token.type == expected_type) return token;
	
	ReportError(token, "Unexpected token '%'. Expected '%'."_sl, token_type_names[(u32)token.type], token_type_names[(u32)expected_type]);
	return {};
}

Token Tokenizer::ExpectKeyword(KeywordType expected_keyword) {
	auto token = FindNextToken();
	if (token.keyword == expected_keyword) return token;
	
	ReportError(token, "Unexpected token '%'. Expected a keyword '%'."_sl, token_type_names[(u32)token.type], keyword_type_names[(u32)expected_keyword]);
	return {};
}


u64 ErrorReportContext::StringToSourceLocation(String string) {
	DebugAssert(string.count == 0 || string.data >= file.data && string.data + string.count <= file.data + file.count, "Source location string '%' is outside of the source file range.", string);
	
	u64 length = Min(string.count, (u64)u16_max);
	u64 offset = length ? Min(string.data - file.data, (u64)u32_max) : 0;
	return (file_index << 48) | (length << 32) | offset;
}

void ErrorReportContext::ReportMessage(StackAllocator* alloc, String string, String message) {
	TempAllocationScope(alloc);
	
	DebugAssert(string.count == 0 || string.data >= file.data && string.data + string.count <= file.data + file.count, "Source location string '%' is outside of the source file range.", string);
	
	StringBuilder builder;
	builder.alloc = alloc;
	
	compile_const u32 tab_width = 4;
	
	u32 line   = 0;
	u32 column = 0;
	const char* file_string = file.data;
	while (file_string < string.data && *file_string) {
		if (*file_string == '\n') {
			line  += 1;
			column = 0;
		} else {
			column += (*file_string == '\t') ? tab_width : 1;
		}
		file_string += 1;
	}
	
	builder.Append("%(%,%): error: %\n"_sl, filepath, line + 1, column + 1, message);
	
	if (string.data != nullptr) {
		auto error_line = string;
		u64 error_line_skip_count = 0;
		
		// Extend the token string to the beginning of the line.
		while (error_line.data > file.data && IsLineEnding(error_line.data[-1]) == false) {
			error_line_skip_count += error_line.data[-1] == '\t' ? tab_width : 1;
			error_line.data  -= 1;
			error_line.count += 1;
		}
		
		// Extend the token string to the end of the line.
		while ((error_line.data + error_line.count) < (file.data + file.count) && IsLineEnding(error_line.data[error_line.count]) == false) {
			error_line.count += 1;
		}
		
		auto highlight_line = StringAllocate(alloc, error_line_skip_count + string.count);
		memset(highlight_line.data, ' ', error_line_skip_count);
		memset(highlight_line.data + error_line_skip_count, '^', string.count);
		
		auto error_line_with_spaces = StringReplaceTabsWithSpaces(alloc, error_line, tab_width);
		builder.Append("%\n%\n"_sl, error_line_with_spaces, highlight_line);
	}
	
	SystemWriteToConsole(builder.ToString());
}

void ErrorReportContext::ReportErrorV(StackAllocator* alloc, String string, String format, ArrayView<StringFormatArgument> arguments) {
	TempAllocationScope(alloc);
	ReportMessage(alloc, string, StringFormatV(alloc, format, arguments));
	SystemExitProcess(1);
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
	"DoubleColon"_sl,
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
	"Union"_sl,
	"Struct"_sl,
	"Static"_sl,
	"Inline"_sl,
	"Template"_sl,
	"Typename"_sl,
	"Namespace"_sl,
	"CompileConst"_sl,
	"Operator"_sl,
	"AlignAs"_sl,
};
static_assert(ArraySize(keyword_type_names) == (u32)KeywordType::Count, "Mismatching keyword_type_names count.");

