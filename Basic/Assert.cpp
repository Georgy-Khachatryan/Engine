#include "Basic.h"
#include "BasicFiles.h"

#if ENABLE_FEATURE(ASSERTS)
#include <stdarg.h>
#include <stdio.h>

compile_const u32 disabled_assert_max_count = 64;
static const char* disabled_assert_formats[disabled_assert_max_count] = {};
static u32 disabled_assert_count = 0;
static u32 disable_all_asserts = 0; // Can be set from the debugger.

bool IsAssertEnabled(const char* format) {
	if (disable_all_asserts != 0) return false;
	
	for (u32 i = 0; i < disabled_assert_count; i += 1) {
		if (disabled_assert_formats[i] == format) return false;
	}
	
	return true;
}

static void DisableAssert(const char* format) {
	u32 index = disabled_assert_count >= disabled_assert_max_count ? disabled_assert_max_count - 1 : disabled_assert_count++;
	disabled_assert_formats[index] = format;
}

#pragma optimize("", off)
void AssertHandler(const char* format, ...) {
	char formatted_error_message_buffer[1024] = {};
	
	String formatted_error_message;
	formatted_error_message.data  = formatted_error_message_buffer;
	formatted_error_message.count = sizeof(formatted_error_message_buffer);
	
	va_list va_args;
	va_start(va_args, format);
	formatted_error_message.count = vsnprintf(formatted_error_message.data, formatted_error_message.count, format, va_args);
	va_end(va_args);
	
	SystemWriteToConsole("Assertion Failed: "_sl);
	SystemWriteToConsole(formatted_error_message);
	
	u32 disable_assert = 0; // Can be set from the debugger.
	__debugbreak();
	
	if (disable_assert != 0) DisableAssert(format);
}
#pragma optimize("", on)
#endif // ENABLE_FEATURE(ASSERTS)
