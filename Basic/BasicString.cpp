#include "BasicString.h"
#include "BasicMath.h"
#include "BasicMemory.h"

#include <stdio.h> // For snprintf

bool String::operator== (String other) const {
	return (count == other.count) && (count == 0 || memcmp(data, other.data, count) == 0);
}

bool String::operator!= (String other) const {
	return (count != other.count) || (count != 0 && memcmp(data, other.data, count) != 0);
}

String StringFormatV(StackAllocator* alloc, String format, ArrayView<StringFormatArgument> arguments) {
	u64 count = StringFormatToMemory(String{}, format, arguments);
	
	auto result = StringAllocate(alloc, count);
	StringFormatToMemory(result, format, arguments);
	
	return result;
}

String StringFromCString(const char* data) {
	return data ? String{ (char*)data, strlen(data) } : String{};
}

String StringCopy(StackAllocator* alloc, String source) {
	if (source.count == 0) return {};
	
	String result;
	result.count = source.count;
	result.data  = (char*)alloc->Allocate(result.count + 1);
	memcpy(result.data, source.data, result.count);
	result.data[result.count] = 0;
	
	return result;
}

String StringCopy(HeapAllocator* alloc, String source) {
	if (source.count == 0) return {};
	
	String result;
	result.count = source.count;
	result.data  = (char*)alloc->Allocate(result.count + 1);
	memcpy(result.data, source.data, result.count);
	result.data[result.count] = 0;
	
	return result;
}

String StringAllocate(StackAllocator* alloc, u64 count) {
	String result;
	result.count = count;
	result.data  = (char*)alloc->Allocate(result.count + 1);
	result.data[result.count] = 0;
	
	return result;
}

String StringAllocate(HeapAllocator* alloc, u64 count) {
	String result;
	result.count = count;
	result.data  = (char*)alloc->Allocate(result.count + 1);
	result.data[result.count] = 0;
	
	return result;
}

String StringReplaceTabsWithSpaces(StackAllocator* alloc, String source, u32 tab_width) {
	u64 result_count = 0;
	for (u64 i = 0; i < source.count; i += 1) {
		result_count += source[i] == '\t' ? tab_width : 1;
	}
	if (result_count == source.count) return source;
	
	auto result = StringAllocate(alloc, result_count);
	for (u64 i = 0, j = 0; i < source.count; i += 1) {
		if (source[i] == '\t') {
			memset(&result[j], ' ', tab_width);
			j += tab_width;
		} else {
			result[j] = source[i];
			j += 1;
		}
	}
	
	return result;
}

String StringJoin(StackAllocator* alloc, ArrayView<String> source_strings, String separator) {
	if (source_strings.count == 0) return ""_sl;
	if (source_strings.count == 1) return source_strings[0];
	
	u64 total_string_length = 0;
	for (auto& string : source_strings) {
		total_string_length += string.count;
	}
	total_string_length += (source_strings.count - 1) * separator.count;
	
	auto result = StringAllocate(alloc, total_string_length);
	
	u64 offset = 0;
	for (auto& string : source_strings) {
		memcpy(result.data + offset, string.data, string.count);
		offset += string.count;
		
		if (offset < result.count && separator.count != 0) {
			memcpy(result.data + offset, separator.data, separator.count);
			offset += separator.count;
		}
	}
	
	return result;
}

struct StringBuilderEntry {
	StringBuilderEntry* next_entry = nullptr;
	u64 size = 0;
};

static String AppendStringBuilderEntry(StringBuilder& builder, u64 size) {
	u64 indentation_level = builder.indentation_level;
	auto* entry = NewInPlace(builder.alloc->Allocate(sizeof(StringBuilderEntry) + size + indentation_level + 1), StringBuilderEntry);
	entry->size = size + indentation_level;
	
	if (builder.head_entry == nullptr) {
		builder.head_entry = entry;
	} else {
		builder.tail_entry->next_entry = entry;
	}
	
	builder.tail_entry = entry;
	builder.total_string_size += size + indentation_level;
	
	if (indentation_level != 0) {
		memset(entry + 1, '\t', indentation_level);
	}
	
	String result;
	result.data  = (char*)(entry + 1) + indentation_level;
	result.count = size;
	result.data[result.count] = 0;
	
	return result;
}

void StringBuilder::AppendV(String format, ArrayView<StringFormatArgument> arguments) {
	auto result = AppendStringBuilderEntry(*this, StringFormatToMemory(String{}, format, arguments));
	StringFormatToMemory(result, format, arguments);
}

void StringBuilder::Append(String string) {
	auto result = AppendStringBuilderEntry(*this, string.count);
	memcpy(result.data, string.data, string.count);
}

void StringBuilder::AppendBuilder(StringBuilder& builder) {
	if (builder.total_string_size == 0) return;
	
	if (head_entry == nullptr) {
		head_entry = builder.head_entry;
	} else {
		tail_entry->next_entry = builder.head_entry;
	}
	
	tail_entry = builder.tail_entry;
	total_string_size += builder.total_string_size;
}

String StringBuilder::ToString(StackAllocator* string_alloc) {
	if (total_string_size == 0) return {};
	
	auto  result = StringAllocate(string_alloc ? string_alloc : alloc, total_string_size);
	char* cursor = result.data;
	
	auto* entry = head_entry;
	while (entry) {
		u64 entry_size = entry->size;
		
		memcpy(cursor, entry + 1, entry_size);
		cursor += entry_size;
		
		entry = entry->next_entry;
	}
	
	return result;
}


// TODO: Signal parse errors.
u64 StringToU64(String string) {
	u64 base = 10;
	if ((string.count > 2) && (string[0] == '0') && (string[1] == 'x' || string[1] == 'X')) {
		base = 16;
		string.data  += 2;
		string.count -= 2;
	}
	
	u64 result = 0;
	for (u64 i = 0; i < string.count; i += 1) {
		auto c = string[i];
		
		// TODO: Experiment with different char to digit conversion schemes.
		u64 digit = u64_max;
		if (CharIsNumeric(c)) {
			digit = c - '0';
		} else if (c >= 'A' && c <= 'F') {
			digit = c - 'A' + 10;
		} else if (c >= 'a' && c <= 'f') {
			digit = c - 'a' + 10;
		}
		
		if (digit >= base) return 0;
		
		// TODO: Handle overflow.
		result = result * base + digit;
	}
	
	return result;
}


// Based on wyhash by Wang Yi (public domain).
static inline u64 WyHashMix(u64 a, u64 b)     { a = _mulx_u64(a, b, &b); return a ^ b; }
static inline u64 WyHashRead8(const u8* data) { u64 v; memcpy(&v, data, 8); return v; }
static inline u64 WyHashRead4(const u8* data) { u32 v; memcpy(&v, data, 4); return v; }
static inline u64 WyHashReadUpTo3(const u8* data, u64 k) { return (((u64)data[0]) << 16) | (((u64)data[k >> 1]) << 8) | data[k - 1]; }

u64 ComputeHash(const u8* data, u64 count, u64 seed) {
	compile_const u64 secret[4] = { 0x2D358DCCAA6C78A5ull, 0x8BB84B93962EACC9ull, 0x4B33A62ED433D4A3ull, 0x4D5A2DA51DE1AA47ull };
	
	seed ^= WyHashMix(seed ^ secret[0], secret[1]);
	
	u64	a;
	u64	b;
	if (count <= 16) {
		if (count >= 4) {
			a = (WyHashRead4(data)             << 32) | WyHashRead4(data             + ((count >> 3) << 2));
			b = (WyHashRead4(data + count - 4) << 32) | WyHashRead4(data + count - 4 - ((count >> 3) << 2));
		} else if (count != 0) {
			a = WyHashReadUpTo3(data, count);
			b = 0;
		} else {
			a = 0;
			b = 0;
		}
	} else {
		u64 index = count; 
		
		if (index >= 48) {
			u64 see1 = seed;
			u64 see2 = seed;
			do {
				seed = WyHashMix(WyHashRead8(data)      ^ secret[1], WyHashRead8(data + 8)  ^ seed);
				see1 = WyHashMix(WyHashRead8(data + 16) ^ secret[2], WyHashRead8(data + 24) ^ see1);
				see2 = WyHashMix(WyHashRead8(data + 32) ^ secret[3], WyHashRead8(data + 40) ^ see2);
				
				data  += 48;
				index -= 48;
			} while (index >= 48);
			
			seed ^= (see1 ^ see2);
		}
		
		while (index > 16) {
			seed = WyHashMix(WyHashRead8(data) ^ secret[1], WyHashRead8(data + 8) ^ seed);
			
			data  += 16;
			index -= 16;
		}
		
		a = WyHashRead8(data + index - 16);
		b = WyHashRead8(data + index - 8);
	}
	
	a ^= secret[1];
	b ^= seed;
	
	a = _mulx_u64(a, b, &b);
	
	return WyHashMix(a ^ secret[0] ^ count, b ^ secret[1]);
}

u64 ComputeHash(String string) { return ComputeHash((u8*)string.data, string.count, 0); }


#define STRING_FORMAT_ARG(value_type, enum_type, union_type) template<> StringFormatArgument StringFormatArgumentFromT<value_type>(value_type value) { StringFormatArgument result; result.type = StringFormatType::enum_type; result.value.union_type = value; return result; }
	STRING_FORMAT_ARG(u8,  U8,  _u64);
	STRING_FORMAT_ARG(u16, U16, _u64);
	STRING_FORMAT_ARG(u32, U32, _u64);
	STRING_FORMAT_ARG(u64, U64, _u64);
	STRING_FORMAT_ARG(s8,  S8,  _s64);
	STRING_FORMAT_ARG(s16, S16, _s64);
	STRING_FORMAT_ARG(s32, S32, _s64);
	STRING_FORMAT_ARG(s64, S64, _s64);
	STRING_FORMAT_ARG(char, Char, _char);
	STRING_FORMAT_ARG(float32, Float32, _float32);
	STRING_FORMAT_ARG(float64, Float64, _float64);
	STRING_FORMAT_ARG(String,  String,  _string);
	STRING_FORMAT_ARG(const char*, CString, _cstring);
	STRING_FORMAT_ARG(const void*, Pointer, _pointer);
#undef STRING_FORMAT_ARG


u64 StringFormatToMemory(String output, String format, ArrayView<StringFormatArgument> arguments) {
	u64 output_index = 0;
	u64 output_size  = 0;
	
	auto copy_string_to_output = [&](String string) {
		u64 count = Min(output.count - output_index, string.count);
		memcpy(output.data + output_index, string.data, count);
		output_index += count;
		output_size  += string.count;
	};
	
	u64 last_copied_index = 0;
	auto copy_format_to_output = [&](u64 index) {
		if (index > last_copied_index) {
			copy_string_to_output(String{ format.data + last_copied_index, index - last_copied_index });
		}
	};
	
	auto format_u64_base_10 = [&](u64 value, char leading_char = '\0') {
		compile_const u64 max_char_count = 32;
		char buffer[max_char_count];
		
		u64 char_index = max_char_count;
		while (value != 0) {
			// TODO: Format 2 digits at a time?
			u64 digit = (value % 10);
			value /= 10;
			
			buffer[--char_index] = '0' + (char)digit;
		}
		
		if (char_index == max_char_count) {
			buffer[--char_index] = '0';
		}
		
		if (leading_char != '\0') {
			buffer[--char_index] = leading_char;
		}
		
		copy_string_to_output(String{ buffer + char_index, max_char_count - char_index });
	};
	
	auto format_u64_base_16 = [&](u64 value, u64 min_char_count = 1) {
		compile_const char* digits = "0123456789ABCDEF";
		
		compile_const u64 max_char_count = 32;
		char buffer[max_char_count];
		
		u64 char_index = max_char_count;
		while (value != 0) {
			u64 digit = (value & 0xF);
			value >>= 4;
			
			buffer[--char_index] = digits[digit];
		}
		
		while (max_char_count - char_index < min_char_count) {
			buffer[--char_index] = '0';
		}
		
		copy_string_to_output(String{ buffer + char_index, max_char_count - char_index });
	};
	
	auto format_float64_base_10 = [&](float64 value) {
		compile_const u64 max_char_count = 32;
		char buffer[max_char_count];
		
		u64 count = snprintf(buffer, max_char_count, "%g", value);
		copy_string_to_output({ buffer, count });
	};
	
	u64 argument_index = 0;
	for (u64 i = 0; i < format.count;) {
		auto c = format[i];
		
		if ((c == '%') && (i + 1 < format.count) && format[i + 1] == '%') { // Handle %% escape sequence. Output one %.
			copy_format_to_output(i + 1);
			last_copied_index = i + 2;
			i += 2;
		} else if (c == '%') {
			copy_format_to_output(i);
			i += 1; // Consume %.
			
			c = i < format.count ? format[i] : '\0';
			if (CharIsNumeric(c)) {
				String number;
				number.data  = format.data + i;
				number.count = 0;
				
				while (i < format.count && CharIsNumeric(format[i])) {
					i += 1;
					number.count += 1;
				}
				c = i < format.count ? format[i] : '\0';
				
				argument_index = StringToU64(number);
			}
			
			bool use_format_base_16 = c == 'x';
			if (use_format_base_16) {
				i += 1;
				c = i < format.count ? format[i] : '\0';
			}
			
			if (c == '.') { // Dot is treated as the last char in a format specifier.
				i += 1;
			}
			
			last_copied_index = i;
			
			// Out of bounds arguments are skipped.
			auto argument = argument_index < arguments.count ? arguments[argument_index++] : StringFormatArgument{};
			switch (argument.type) {
			case StringFormatType::U8:
			case StringFormatType::U16:
			case StringFormatType::U32:
			case StringFormatType::U64: {
				if (use_format_base_16) {
					format_u64_base_16(argument.value._u64);
				} else {
					format_u64_base_10(argument.value._u64);
				}
				break;
			} case StringFormatType::S8:
			case StringFormatType::S16:
			case StringFormatType::S32:
			case StringFormatType::S64: {
				if (use_format_base_16) {
					switch (argument.type) {
					case StringFormatType::S8:  format_u64_base_16((u8)argument.value._u64);  break;
					case StringFormatType::S16: format_u64_base_16((u16)argument.value._u64); break;
					case StringFormatType::S32: format_u64_base_16((u32)argument.value._u64); break;
					case StringFormatType::S64: format_u64_base_16((u64)argument.value._u64); break;
					}
				} else {
					bool is_negative = argument.value._s64 < 0;
					format_u64_base_10(is_negative ? -argument.value._s64 : argument.value._s64, is_negative ? '-' : '\0');
				}
				break;
			} case StringFormatType::Float32: {
				if (use_format_base_16) {
					format_u64_base_16((u32)argument.value._u64, 8);
				} else {
					format_float64_base_10(argument.value._float32);
				}
				break;
			} case StringFormatType::Float64: {
				if (use_format_base_16) {
					format_u64_base_16(argument.value._u64, 16);
				} else {
					format_float64_base_10(argument.value._float64);
				}
				break;
			} case StringFormatType::Char: {
				copy_string_to_output(String{ &argument.value._char, 1 });
				break;
			} case StringFormatType::String: {
				copy_string_to_output(argument.value._string);
				break;
			} case StringFormatType::CString: {
				copy_string_to_output(StringFromCString(argument.value._cstring));
				break;
			} case StringFormatType::Pointer: {
				format_u64_base_16((u64)argument.value._pointer, 16);
				break;
			}
			}
		} else {
			i += 1;
		}
	}
	copy_format_to_output(format.count);
	
	return output_size;
}

