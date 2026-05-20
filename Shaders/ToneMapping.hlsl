#include "Basic.hlsl"
#include "ToneMappingGT7.hlsl"

compile_const float3x3 rec709_to_rec2020 = float3x3(
	float3(+0.627403895934699030, +0.329283038377883750, +0.043313065687417274),
	float3(+0.069097289358231992, +0.919540395075459040, +0.011362315566309154),
	float3(+0.016391438875150228, +0.088013307877225777, +0.895595253247624010)
);

compile_const float3x3 rec2020_to_rec709 = float3x3(
	float3(+1.660491002108434700, -0.58764113878854962, -0.0728498633198848560),
	float3(-0.124550474521590520, +1.13289989712596010, -0.0083494226043694872),
	float3(-0.018150763354905220, -0.10057889800800737, +1.1187296613629125000)
);

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
