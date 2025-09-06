#pragma once

#include <string.h>

using u8 = unsigned char;
using s8 = signed char;

using u16 = unsigned short;
using s16 = signed short;

using u32 = unsigned int;
using s32 = signed int;

using u64 = unsigned long long;
using s64 = signed long long;


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
