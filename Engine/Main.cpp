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

#if 1
	for (u32 i = 0; i < 2; i += 1) {
		DebugAssert(false, "Assert from %s", __FUNCTION__);
		DebugAssertOnce(false, "AssertOnce from %s", __FUNCTION__);
		DebugAssertAlways("AssertAlways from %s", __FUNCTION__);
	}
#endif

	defer{ printf("Deferred\n"); };
	printf("Regular\n");
}
