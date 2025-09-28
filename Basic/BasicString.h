#pragma once
#include "Basic.h"
#include "BasicArray.h"

#include <stdarg.h>

struct String {
	char* data  = nullptr;
	u64 count = 0;
	
	bool operator== (String other) const;
	bool operator!= (String other) const;
	
	char& operator[] (u64 index) { DebugAssert(index < count, "String access out of bounds. %llu/%llu.", index, count); return data[index]; }
	const char& operator[] (u64 index) const { DebugAssert(index < count, "String access out of bounds. %llu/%llu.", index, count); return data[index]; }
};
using StringUtf8 = String;

struct StringUtf16 {
	u16* data = nullptr;
	u64 count = 0;
};

inline constexpr StringUtf8 operator""_sl(const char* data, u64 count) { return { (char*)data, count }; }

StringUtf8  StringUtf16ToUtf8(StackAllocator* alloc, StringUtf16 string);
StringUtf16 StringUtf8ToUtf16(StackAllocator* alloc, StringUtf8  string);
String StringFormatV(StackAllocator* alloc, const char* format, va_list args);
String StringFormat(StackAllocator* alloc, const char* format, ...);
String StringCopy(HeapAllocator* alloc, String source);
String StringAllocate(StackAllocator* alloc, u64 count);
String StringReplaceTabsWithSpaces(StackAllocator* alloc, String source, u32 tab_width);
String StringJoin(StackAllocator* alloc, ArrayView<String> source_strings, String separator = ""_sl);


struct StringBuilderEntry;

struct StringBuilder {
	StackAllocator* alloc = nullptr;
	
	StringBuilderEntry* head_entry = nullptr;
	StringBuilderEntry* tail_entry = nullptr;
	u64 total_string_size = 0;
	u64 indentation_level = 0;
	
	void Append(const char* format, ...);
	void AppendV(const char* format, va_list args);
	void AppendUnformatted(String string);
	
	void Indent()   { indentation_level += 1; }
	void Unindent() { DebugAssert(indentation_level != 0, "Mismatched Indent/Unindent."); indentation_level -= 1; }
	
	String ToString(StackAllocator* string_alloc = nullptr);
};


u64 ComputeHash(const u8* data, u64 count, u64 seed = 0);
u64 ComputeHash(String string);

