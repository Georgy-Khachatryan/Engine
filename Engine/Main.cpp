#include "Basic/Basic.h"

#include <stdio.h>

s32 main() {
	static_assert(sizeof(s8) == 1);
	static_assert(sizeof(u8) == 1);

	static_assert(sizeof(s16) == 2);
	static_assert(sizeof(u16) == 2);

	static_assert(sizeof(s32) == 4);
	static_assert(sizeof(u32) == 4);

	static_assert(sizeof(s64) == 8);
	static_assert(sizeof(u64) == 8);

	defer{ printf("Deferred\n"); };
	printf("Regular\n");
}
