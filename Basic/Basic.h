#pragma once

#include <string.h>

#define BUILD_TYPE(x) BUILD_TYPE_DEFINITION_##x ()
#if defined(BUILD_TYPE_DEBUG)
#define BUILD_TYPE_DEFINITION_DEBUG() 1
#define BUILD_TYPE_DEFINITION_DEV() 0
#define BUILD_TYPE_DEFINITION_PROFILE() 0
#elif defined(BUILD_TYPE_DEV)
#define BUILD_TYPE_DEFINITION_DEBUG() 0
#define BUILD_TYPE_DEFINITION_DEV() 1
#define BUILD_TYPE_DEFINITION_PROFILE() 0
#elif defined(BUILD_TYPE_PROFILE)
#define BUILD_TYPE_DEFINITION_DEBUG() 0
#define BUILD_TYPE_DEFINITION_DEV() 0
#define BUILD_TYPE_DEFINITION_PROFILE() 1
#else // !defined(BUILD_TYPE_PROFILE)
#error "Unknown BUILD_TYPE. Must be set to either DEBUG, DEV, or PROFILE."
#endif // defined(BUILD_TYPE_PROFILE)

#define ENABLE_FEATURE(x) ENABLE_FEATURE_DEFINITION_##x ()
#if BUILD_TYPE(DEBUG)
#define ENABLE_FEATURE_DEFINITION_ASSERTS() 1
#elif BUILD_TYPE(DEV)
#define ENABLE_FEATURE_DEFINITION_ASSERTS() 1
#elif BUILD_TYPE(PROFILE)
#define ENABLE_FEATURE_DEFINITION_ASSERTS() 0
#else // !BUILD_TYPE(PROFILE)
#error "Unknown BUILD_TYPE. Must be set to either DEBUG, DEV, or PROFILE."
#endif // !BUILD_TYPE(PROFILE)


using u8  = unsigned char;
using s8  = signed char;
using u16 = unsigned short;
using s16 = signed short;
using u32 = unsigned int;
using s32 = signed int;
using u64 = unsigned long long;
using s64 = signed long long;

#define compile_const constexpr static const


#if ENABLE_FEATURE(ASSERTS)
bool IsAssertEnabled(const char* format);
void AssertHandler(const char* format, ...);
#endif // ENABLE_FEATURE(ASSERTS)

#if ENABLE_FEATURE(ASSERTS)
#define DebugAssert(expression, format, ...)     do { if (!(expression) && IsAssertEnabled(format)) { AssertHandler(format, __VA_ARGS__); } } while (false)
#define DebugAssertOnce(expression, format, ...) do { static bool is_enabled = true; if (is_enabled && !(expression) && IsAssertEnabled(format)) { is_enabled = false; AssertHandler(format, __VA_ARGS__); } } while (false)
#define DebugAssertAlways(format, ...)           do { if (IsAssertEnabled(format)) { AssertHandler(format, __VA_ARGS__); } } while (false)
#else // !ENABLE_FEATURE(ASSERTS)
#define DebugAssert(expression, format, ...)     do { } while (false)
#define DebugAssertOnce(expression, format, ...) do { } while (false)
#define DebugAssertAlways(format, ...)           do { } while (false)
#endif // !ENABLE_FEATURE(ASSERTS)


#define PREPROCESSOR_MERGE_TOKENS1(lh, rh) lh##rh
#define PREPROCESSOR_MERGE_TOKENS0(lh, rh) PREPROCESSOR_MERGE_TOKENS1(lh, rh)
#define CREATE_UNIQUE_NAME(prefix) PREPROCESSOR_MERGE_TOKENS0(prefix, __COUNTER__)


template<typename Lambda>
struct DeferredLambda {
	DeferredLambda(Lambda&& lambda) : lambda(static_cast<Lambda&&>(lambda)) {}
	~DeferredLambda() { lambda(); }
	Lambda lambda;
};
enum struct DeferredLambdaToken {};

template<typename Lambda>
inline DeferredLambda<Lambda> operator+ (DeferredLambdaToken, Lambda&& lambda) { return DeferredLambda<Lambda>(static_cast<Lambda&&>(lambda)); }

#define defer auto CREATE_UNIQUE_NAME(deferred_lambda_) = DeferredLambdaToken{} + [&]()
