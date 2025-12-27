#pragma once
#include "Basic.h"
#include "BasicArray.h"

struct String {
	char* data = nullptr;
	u64  count = 0;
	
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
String StringFromCString(const char* data);
String StringCopy(StackAllocator* alloc, String source);
String StringCopy(HeapAllocator* alloc, String source);
String StringAllocate(StackAllocator* alloc, u64 count);
String StringAllocate(HeapAllocator* alloc, u64 count);
String StringReplaceTabsWithSpaces(StackAllocator* alloc, String source, u32 tab_width);
String StringJoin(StackAllocator* alloc, ArrayView<String> source_strings, String separator = ""_sl);
u64 StringFormatToMemory(String output, String format, ArrayView<StringFormatArgument> arguments);
String StringFormatV(StackAllocator* alloc, String format, ArrayView<StringFormatArgument> arguments);
template<typename ... Args> String StringFormat(StackAllocator* alloc, String format, Args ... args) { FORMAT_PROC_BODY(StringFormatV, alloc, format); }


struct StringBuilderEntry;

struct StringBuilder {
	StackAllocator* alloc = nullptr;
	
	StringBuilderEntry* head_entry = nullptr;
	StringBuilderEntry* tail_entry = nullptr;
	u64 total_string_size = 0;
	u64 indentation_level = 0;
	
	void AppendV(String format, ArrayView<StringFormatArgument> arguments);
	template<typename ... Args> void Append(String format, Args ... args) { FORMAT_PROC_BODY(AppendV, format); }
	
	void Append(String string); // Explicit single string overload significantly improves compile times.
	void AppendBuilder(StringBuilder& builder);
	
	void Indent()   { indentation_level += 1; }
	void Unindent() { DebugAssert(indentation_level != 0, "Mismatched Indent/Unindent."); indentation_level -= 1; }
	
	String ToString(StackAllocator* string_alloc = nullptr);
};


u64 StringToU64(String string);

u64 ComputeHash(const u8* data, u64 count, u64 seed = 0);
u64 ComputeHash(String string);


inline char CharToUpperCase(char c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }
inline char CharIsUpperCase(char c) { return (c >= 'A' && c <= 'Z'); }
inline char CharIsLowerCase(char c) { return (c >= 'a' && c <= 'z'); }
inline bool CharIsNumeric(char c)   { return (c >= '0' && c <= '9'); }


enum struct StringFormatType : u32 {
	None    = 0,
	S8      = 1,
	U8      = 2,
	S16     = 3,
	U16     = 4,
	S32     = 5,
	U32     = 6,
	U64     = 7,
	S64     = 8,
	Float32 = 9,
	Float64 = 10,
	Char    = 11,
	String  = 12,
	CString = 13,
	Pointer = 14,
	
	Count
};

struct StringFormatArgument {
	StringFormatType type = StringFormatType::None;
	
	union {
		u64         _u64;
		s64         _s64;
		float32     _float32;
		float64     _float64;
		char        _char;
		String      _string;
		const char* _cstring;
		const void* _pointer;
	} value = {};
};
