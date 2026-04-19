#include "Basic.hlsl"
#include "SDK/NvAPI/include/nvHLSLExtns.h"

u64 GetGlobalTimer() {
	return (u64)NvGetSpecial(NV_SPECIALOP_GLOBAL_TIMER_LO) | ((u64)NvGetSpecial(NV_SPECIALOP_GLOBAL_TIMER_HI) << 32);
}

#define VISUALIZATION_TYPE 3
#define SHOW_WIREFRAME 0

[ThreadGroupSize(32, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	if (VISUALIZATION_TYPE >= 3) return;
	
	uint2  thread_id = group_id * uint2(8, 4) + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	ray_desc.TMin      = 0.0;
	ray_desc.TMax      = 1024.0;
	
	uint sample_count = VISUALIZATION_TYPE == 0 ? 16 : 1;
	
	uint meshlet_header_offset = 0;
	float3 barycentrics = 0.0;
	
	u64 t0 = GetGlobalTimer();
	[loop]
	for (uint i = 0; i < sample_count; i += 1) {
		RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES /*| RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH*/> ray_query;
		ray_query.TraceRayInline(scene_tlas, 0, 0xFF, ray_desc);
		
		while (ray_query.Proceed()) {
			
		}
		
		meshlet_header_offset = NvRtGetCommittedClusterID(ray_query);
		if (ray_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
			barycentrics.yz = ray_query.CommittedTriangleBarycentrics();
		}
	}
	u64 t1 = GetGlobalTimer();
	
	if (meshlet_header_offset != u32_max) {
#if SHOW_WIREFRAME
		barycentrics.x = 1.0 - barycentrics.y - barycentrics.z;
		float wireframe = BarycentricWireframe(barycentrics, 0.05, 0.05);
#else // !SHOW_WIREFRAME
		float wireframe = 1.0;
#endif // !SHOW_WIREFRAME
		
#if (VISUALIZATION_TYPE == 0)
		float delta_time = (uint)(t1 - t0) * (1.0 / 20000.0) * (1.0 / sample_count);
		scene_radiance[thread_id] = float4(PlasmaHeatMap(saturate(delta_time)) * wireframe, 1.0);
#elif (VISUALIZATION_TYPE == 1)
		scene_radiance[thread_id] = float4(DecodeSRGB(RandomColor(meshlet_header_offset >> 4)) * wireframe, 1.0);
#elif (VISUALIZATION_TYPE == 2)
		scene_radiance[thread_id] = float4(DecodeSRGB(RandomColor(meshlet_header_offset >> 17)) * wireframe, 1.0);
#endif // (VISUALIZATION_TYPE == 1)
	}
}
