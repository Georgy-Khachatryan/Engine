#include "Basic.hlsl"
#include "ToneMappingGT7.hlsl"
#include "ColorSpaces.hlsl"

float3 ApplyToneMapping(float3 radiance_rec2020) {
	float3 result_rec2020;
	
	if (constants.method == ToneMappingMethod::GT7_HDR || constants.method == ToneMappingMethod::GT7_SDR) {
		result_rec2020 = ApplyToneMappingGT7(constants, radiance_rec2020);
	} else if (constants.method == ToneMappingMethod::Reinhard_SDR) {
		result_rec2020 = radiance_rec2020 / (radiance_rec2020 + 1.0);
	} else {
		result_rec2020 = radiance_rec2020;
	}
	
	return result_rec2020;
}

float3 LoadSceneRadiance(uint2 thread_id) {
	float3 radiance_rec709 = scene_radiance[thread_id].xyz * exposure[0] * scene.inv_exposure_estimate;
	
	// Downsample in linear space before tone mapping.
	if (constants.use_external_output) {
		radiance_rec709 += WaveShuffleXor(radiance_rec709, 0x1);
		radiance_rec709 += WaveShuffleXor(radiance_rec709, 0x2);
		radiance_rec709 += WaveShuffleXor(radiance_rec709, 0x4);
		radiance_rec709 += WaveShuffleXor(radiance_rec709, 0x8);
		radiance_rec709 *= (1.0 / 16.0);
	}
	
	return radiance_rec709;
}

compile_const u32 thread_group_size = ToneMappingGpuConstants::thread_group_size;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)][WaveSize(16, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	float3 radiance_rec709  = LoadSceneRadiance(thread_id);
	float3 radiance_rec2020 = mul(rec709_to_rec2020, radiance_rec709);
	
	float3 result_rec2020 = ApplyToneMapping(radiance_rec2020);
	float3 result_rec709  = mul(rec2020_to_rec709, result_rec2020);
	
	if (constants.use_external_output == 0) {
		scene_radiance[thread_id] = float4(result_rec709, 1.0);
	} else if ((thread_index & 0xF) == 0) {
		external_output[thread_id / 4 + constants.output_offset] = float4(EncodeSRGB(result_rec709), 1.0);
	}
}
