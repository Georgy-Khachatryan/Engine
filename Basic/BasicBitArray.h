#pragma once
#include "Basic.h"
#include "BasicArray.h"
#include "BasicMath.h"


inline void BitArraySetBit(ArrayView<u64> mask, u64 index) {
	mask[index / 64u] |= 1ull << (index % 64u);
}

inline void BitArrayResetBit(ArrayView<u64> mask, u64 index) {
	mask[index / 64u] &= ~(1ull << (index % 64u));
}

inline bool BitArrayTestBit(ArrayView<u64> mask, u64 index) {
	return ((mask[index / 64u] >> (index % 64u)) & 0x1) != 0;
}

inline void BitArraySetBitRange(ArrayView<u64> mask, u64 offset, u64 count) {
	if (count == 0) return;
	
	u64 bit_index_0 = offset % 64u;
	u64 bit_index_1 = (offset + count - 1) % 64u;
	
	u64 qword_index_0 = offset / 64u;
	u64 qword_index_1 = (offset + count - 1) / 64u;
	
	if (qword_index_0 == qword_index_1) {
		mask[qword_index_0] |= CreateBitMask(count) << bit_index_0;
	} else {
		mask[qword_index_0] |= ~CreateBitMask(bit_index_0);
		for (u64 i = qword_index_0 + 1; i < qword_index_1; i += 1) {
			mask[i] = u64_max;
		}
		mask[qword_index_1] |= CreateBitMask(bit_index_1 + 1);
	}
}

inline u64 BitArrayCountSetBits(ArrayView<u64> mask) {
	u64 bit_count = 0;
	for (u64 i = 0; i < mask.count; i += 1) {
		bit_count += CountSetBits(mask[i]);
	}
	return bit_count;
}

inline u64 BitArrayCountSetBitsAndNonZeroQwords(ArrayView<u64> mask, u64& set_qword_count) {
	u64 bit_count   = 0;
	u64 qword_count = 0;
	
	for (u64 i = 0; i < mask.count; i += 1) {
		u64 qword = mask[i];
		
		bit_count   += CountSetBits(qword);
		qword_count += (qword != 0);
	}
	
	set_qword_count = qword_count;
	
	return bit_count;
}

struct BitArrayIt {
	BitArrayIt(ArrayView<u64> mask) : data(mask.data), count(mask.count) {}
	
	u64* data = 0;
	u64 count = 0;
	
	struct Iterator {
		u64* data = nullptr;
		u64 count = 0;
		u64 index = 0;
		u64 mask  = 0;
		
		Iterator& operator++ () {
			mask &= (mask - 1);
			if (mask == 0) {
				index += 1;
				FindNextActiveMask();
			}
			return *this;
		}
		
		Iterator& FindNextActiveMask() {
			while (index < count && (mask = data[index]) == 0) {
				index += 1;
			}
			return *this;
		}
		
		bool operator!= (const Iterator&) const { return (mask != 0) || (index < count); }
		u64 operator* () { return FirstBitLow(mask) + index * 64u; };
	};
	
	Iterator begin() { return Iterator{ data, count }.FindNextActiveMask(); }
	Iterator end() { return {}; }
};
