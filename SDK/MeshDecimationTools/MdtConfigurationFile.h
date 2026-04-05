#pragma once
#include "Basic/Basic.h"

#define MDT_MAX_ATTRIBUTE_STRIDE_DWORDS 8
#define MDT_MAX_MESHLET_VERTEX_COUNT 128
#define MDT_MAX_MESHLET_FACE_COUNT 128
#define MDT_PROFILER_SCOPE(name) ProfilerScope(name)
#define MDT_ASSERT(condition) DebugAssert(condition, "MDT_ASSERT failed.")
