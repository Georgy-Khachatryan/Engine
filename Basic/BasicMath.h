#pragma once
#include "Basic.h"

#include <immintrin.h>


inline u32 FirstBitLow32(u32 mask)  { return _tzcnt_u32(mask); }
inline u32 FirstBitHigh32(u32 mask) { return 31 - _lzcnt_u32(mask); }
inline u32 CountSetBits32(u32 mask) { return _mm_popcnt_u32(mask); }
inline u32 CountLeadingZeros32(u32 mask) { return _lzcnt_u32(mask); }
inline bool IsPowerOfTwo32(u32 value) { return CountSetBits32(value) == 1; }
inline u32 RoundUpToPowerOfTwo32(u32 value) { return 1u << (32 - CountLeadingZeros32(value - 1)); }

inline u64 FirstBitLow(u64 mask)  { return _tzcnt_u64(mask); }
inline u64 FirstBitHigh(u64 mask) { return 63 - _lzcnt_u64(mask); }
inline u64 CountSetBits(u64 mask) { return _mm_popcnt_u64(mask); }
inline u64 CountLeadingZeros(u64 mask) { return _lzcnt_u64(mask); }
inline bool IsPowerOfTwo(u64 value) { return CountSetBits(value) == 1; }
inline u64 RoundUpToPowerOfTwo(u64 value) { return 1llu << (64 - CountLeadingZeros(value - 1)); }

inline u64 AlignUp(u64 size, u64 alignment) {
	DebugAssert(IsPowerOfTwo(alignment), "Invalid alignment '0x%llX'. Alignment must be a power of 2.", alignment);
	return (size + alignment - 1) & ~(alignment - 1);
}

inline u64 Min(u64 lh, u64 rh) { return lh < rh ? lh : rh; }
inline u64 Max(u64 lh, u64 rh) { return lh > rh ? lh : rh; }

template<typename T, T(FirstBitLowT)(T)>
struct BitScanLowT {
	explicit BitScanLowT(T mask) : mask(mask) {}
	T mask = 0;
	
	struct Iterator {
		T mask = 0;
		
		Iterator& operator++ () { mask &= (mask - 1); return *this; }
		bool operator!= (const Iterator&) const { return mask != 0; }
		T operator* () { return FirstBitLowT(mask); };
	};
	
	Iterator begin() const { return Iterator{ mask }; }
	Iterator end()   const { return {}; }
};
using BitScanLow   = BitScanLowT<u64, FirstBitLow>;
using BitScanLow32 = BitScanLowT<u32, FirstBitLow32>;

