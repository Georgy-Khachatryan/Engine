#ifndef PARALLELREDUCTION_HLSL
#define PARALLELREDUCTION_HLSL
#include "Basic.hlsl"

// Down sample 64 input samples in 16 lanes to a single value and write 3 MIPs.
template<typename SettingsT>
float WaveDownSample64x3(uint2 thread_id, uint thread_index, uint result_offset, float sample0, float sample1, float sample2, float sample3) {
	float sample = SettingsT::Reduce(SettingsT::Reduce(sample0, sample1), SettingsT::Reduce(sample2, sample3));
	
	SettingsT::StoreResult(result_offset + 0, thread_id >> 0, sample);
	
	sample = SettingsT::Reduce(sample, WaveShuffleXor(sample, 0x1));
	sample = SettingsT::Reduce(sample, WaveShuffleXor(sample, 0x2));
	
	if ((thread_index & 0x3) == 0) {
		SettingsT::StoreResult(result_offset + 1, thread_id >> 1, sample);
	}
	
	sample = SettingsT::Reduce(sample, WaveShuffleXor(sample, 0x4));
	sample = SettingsT::Reduce(sample, WaveShuffleXor(sample, 0x8));
	
	if ((thread_index & 0xF) == 0) {
		SettingsT::StoreResult(result_offset + 2, thread_id >> 2, sample);
	}
	
	return sample;
}

// Down sample 4096 input samples in 1024 lanes to a single value and write 6 MIPs.
template<typename SettingsT>
float GroupDownSample4096x6(uint2 thread_id, uint2 group_id, uint thread_index, uint result_offset, float sample0, float sample1, float sample2, float sample3) {
	float sample = WaveDownSample64x3<SettingsT>(thread_id, thread_index, result_offset, sample0, sample1, sample2, sample3);
	
	if ((thread_index & 0xF) == 0) {
		SettingsT::StoreGroupShared(thread_index >> 4, sample);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (thread_index < 16) {
		thread_id = group_id * 4 + MortonDecode(thread_index);
		
		sample0 = SettingsT::LoadGroupShared(thread_index * 4 + 0);
		sample1 = SettingsT::LoadGroupShared(thread_index * 4 + 1);
		sample2 = SettingsT::LoadGroupShared(thread_index * 4 + 2);
		sample3 = SettingsT::LoadGroupShared(thread_index * 4 + 3);
		sample = WaveDownSample64x3<SettingsT>(thread_id, thread_index, result_offset + 3, sample0, sample1, sample2, sample3);
	}
	
	return sample;
}

#endif // PARALLELREDUCTION_HLSL
