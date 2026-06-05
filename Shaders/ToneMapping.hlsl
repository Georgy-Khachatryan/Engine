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

compile_const u32 thread_group_size = ToneMappingGpuConstants::thread_group_size;

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	
	float3 radiance_rec709  = scene_radiance[thread_id].xyz * exposure[0] * scene.inv_exposure_estimate;
	float3 radiance_rec2020 = mul(rec709_to_rec2020, radiance_rec709);
	
	float3 result_rec2020 = ApplyToneMapping(radiance_rec2020);
	float3 result_rec709  = mul(rec2020_to_rec709, result_rec2020);
	
	scene_radiance[thread_id] = float4(result_rec709, 1.0);
}
