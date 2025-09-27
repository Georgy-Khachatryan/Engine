#include "Basic/Basic.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicHashTable.h"
#include "Tokens.h"



s32 main() {
	auto alloc = CreateStackAllocator(64 * 1024 * 1024, 512 * 1024);
	defer{ ReleaseStackAllocator(alloc); };
	
	auto filepath = "./Engine/RenderPasses.h"_sl;
	auto file = SystemReadFileToString(&alloc, filepath);
	if (file.data == nullptr) {
		SystemWriteToConsole(&alloc, "\x1B[31mFailed to open file '%s'.\x1B[0m\n", filepath.data);
		return 1;
	}
	
	Tokenizer tokenizer;
	tokenizer.file     = file;
	tokenizer.filepath = filepath;
	tokenizer.string   = file.data;
	tokenizer.alloc    = &alloc;
	
	auto token = tokenizer.FindNextToken();
	while (token.type != TokenType::None) {
		SystemWriteToConsole(&alloc, "TokenType: '%s', String: '%.*s'\n", token_type_names[(u32)token.type].data, (s32)token.string.count, token.string.data);
		
		if (token.keyword == KeywordType::Notes) {
			tokenizer.ReportError(token, "Found notes! TokenTypeIndex: '%u'.", (u32)token.type);
		}
		
		token = tokenizer.FindNextToken();
	}
	
	return 0;
}

