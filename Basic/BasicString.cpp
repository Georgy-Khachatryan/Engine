#include "BasicString.h"
#include "BasicMemory.h"

#include <stdio.h>

bool String::operator== (String other) const {
	return (count == other.count) && (count == 0 || memcmp(data, other.data, count) == 0);
}

bool String::operator!= (String other) const {
	return (count != other.count) || (count != 0 && memcmp(data, other.data, count) != 0);
}

String StringFormatV(StackAllocator* alloc, const char* format, va_list args) {
	String result;
	result.count = vsnprintf(nullptr, 0, format, args);
	result.data  = (char*)alloc->Allocate(result.count + 1);
	
	vsnprintf((char*)result.data, result.count + 1, format, args);
	
	return result;
}

String StringFormat(StackAllocator* alloc, const char* format, ...) {
	va_list va_args;
	va_start(va_args, format);
	auto result = StringFormatV(alloc, format, va_args);
	va_end(va_args);
	
	return result;
}

