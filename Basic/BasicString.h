#pragma once
#include "Basic.h"

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

u64 ComputeHash(const u8* data, u64 count, u64 seed = 0);
u64 ComputeHash(String string);

String StringCopy(HeapAllocator* alloc, String source);
