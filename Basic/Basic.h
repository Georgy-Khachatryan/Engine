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


#define compile_const constexpr static const
#define always_inline __forceinline

using u8  = unsigned char;
using s8  = signed char;
using u16 = unsigned short;
using s16 = signed short;
using u32 = unsigned int;
using s32 = signed int;
using u64 = unsigned long long;
using s64 = signed long long;
using float32 = float;
using float64 = double;

compile_const u8  u8_max  = (u8)0xFF;
compile_const u8  u8_min  = (u8)0x00;
compile_const s8  s8_max  = (s8)0x7F;
compile_const s8  s8_min  = (s8)0x80;
compile_const u16 u16_max = (u16)0xFFFF;
compile_const u16 u16_min = (u16)0x0000;
compile_const s16 s16_max = (s16)0x7FFF;
compile_const s16 s16_min = (s16)0x8000;
compile_const u32 u32_max = (u32)0xFFFF'FFFF;
compile_const u32 u32_min = (u32)0x0000'0000;
compile_const s32 s32_max = (s32)0x7FFF'FFFF;
compile_const s32 s32_min = (s32)0x8000'0000;
compile_const u64 u64_max = (u64)0xFFFF'FFFF'FFFF'FFFF;
compile_const u64 u64_min = (u64)0x0000'0000'0000'0000;
compile_const s64 s64_max = (s64)0x7FFF'FFFF'FFFF'FFFF;
compile_const s64 s64_min = (s64)0x8000'0000'0000'0000;


#if ENABLE_FEATURE(ASSERTS)
bool IsAssertEnabled(const char* format);
void AssertHandler(const char* format, ...);
#endif // ENABLE_FEATURE(ASSERTS)

void SystemExitProcess(u32 exit_code);

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


#define ENUM_FLAGS_OPERATORS(EnumTypeT) \
inline constexpr EnumTypeT operator&  (EnumTypeT  lh, EnumTypeT rh) { return (EnumTypeT)((u64)lh & (u64)rh); } \
inline constexpr EnumTypeT operator|  (EnumTypeT  lh, EnumTypeT rh) { return (EnumTypeT)((u64)lh | (u64)rh); } \
inline constexpr EnumTypeT operator&= (EnumTypeT& lh, EnumTypeT rh) { return lh = (EnumTypeT)((u64)lh & (u64)rh); } \
inline constexpr EnumTypeT operator|= (EnumTypeT& lh, EnumTypeT rh) { return lh = (EnumTypeT)((u64)lh | (u64)rh); } \
inline constexpr EnumTypeT operator~  (EnumTypeT lh)                { return (EnumTypeT)~(u64)lh; } \
inline constexpr bool HasAnyFlags(EnumTypeT mask, EnumTypeT test_pattern) { return ((u64)mask & (u64)test_pattern) != 0; } \
inline constexpr bool HasAllFlags(EnumTypeT mask, EnumTypeT test_pattern) { return ((u64)mask & (u64)test_pattern) == (u64)test_pattern; }


template<typename T, u32 size>
inline constexpr u32 ArraySize(const T(&)[size]) { return size; }

template<typename T>
inline constexpr void Swap(T& lh, T& rh) { auto temp = lh; lh = rh; rh = temp; }


struct StackAllocator;
struct HeapAllocator;


#define NOTES(...)


template<typename T> struct SaveLoadAsBytes { compile_const bool value = false; };
#define SAVE_LOAD_AS_BYTES(T) template<> struct SaveLoadAsBytes<T> { using ValueType = T; compile_const bool value = true; }

SAVE_LOAD_AS_BYTES(bool);
SAVE_LOAD_AS_BYTES(u8);
SAVE_LOAD_AS_BYTES(s8);
SAVE_LOAD_AS_BYTES(u16);
SAVE_LOAD_AS_BYTES(s16);
SAVE_LOAD_AS_BYTES(u32);
SAVE_LOAD_AS_BYTES(s32);
SAVE_LOAD_AS_BYTES(u64);
SAVE_LOAD_AS_BYTES(s64);
SAVE_LOAD_AS_BYTES(float32);
SAVE_LOAD_AS_BYTES(float64);


struct StringFormatArgument;
template<typename T> StringFormatArgument StringFormatArgumentFromT(T);

#define FORMAT_PROC_BODY(name, ...) \
StringFormatArgument arguments_array[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = { StringFormatArgumentFromT<Args>(args) ... };\
return name(__VA_ARGS__, { arguments_array, sizeof...(Args) })

