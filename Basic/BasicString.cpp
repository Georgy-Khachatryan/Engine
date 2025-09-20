#include "BasicString.h"
#include "BasicMath.h"
#include "BasicMemory.h"

#include <stdio.h>

bool String::operator== (String other) const {
	return (count == other.count) && (count == 0 || memcmp(data, other.data, count) == 0);
}

bool String::operator!= (String other) const {
	return (count != other.count) || (count != 0 && memcmp(data, other.data, count) != 0);
}

String StringFormatV(StackAllocator* alloc, const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	
	String result;
	result.count = vsnprintf(nullptr, 0, format, args);
	result.data  = (char*)alloc->Allocate(result.count + 1);
	
	vsnprintf((char*)result.data, result.count + 1, format, args_copy);
	va_end(args_copy);
	
	return result;
}

String StringFormat(StackAllocator* alloc, const char* format, ...) {
	va_list va_args;
	va_start(va_args, format);
	auto result = StringFormatV(alloc, format, va_args);
	va_end(va_args);
	
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
