#include "Basic.hlsl"
#include "TextureSampling.hlsl"

#if defined(TRACE_PREVIEW)
compile_const u32 thread_group_size = 16;
compile_const u32 thread_group_area = thread_group_size * thread_group_size;

compile_const float3 terrain_world_space_position = float3(-256.0, -256.0, 0.0);
compile_const float  terrain_world_space_size     = +512.0;
compile_const float  inv_terrain_world_space_size = 1.0 / terrain_world_space_size;
compile_const float  height_field_offset          = 64.0;

// Based on the ideas from https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/
float TraceRay(RayDesc ray_desc) {
	float3 origin;
	origin.xy = (ray_desc.Origin.xy - terrain_world_space_position.xy) * inv_terrain_world_space_size + 1.0;
	origin.z  = (ray_desc.Origin.z  - terrain_world_space_position.z - height_field_offset);
	
	float3 direction;
	direction.xy = ray_desc.Direction.xy;
	direction.z  = ray_desc.Direction.z * terrain_world_space_size;
	
	float t_min = ray_desc.TMin * inv_terrain_world_space_size;
	float t_max = ray_desc.TMax * inv_terrain_world_space_size;
	
	float3 inv_direction = select(direction == 0.0, /*nan*/asfloat(0x7FC00000), 1.0 / direction); // See @inv_direction for reference.
	
	float  ray_t     = t_min;
	float3 position  = float3(clamp(origin.xy + direction.xy * ray_t, 0.0, asfloat(0x3FFFFFFF)), origin.z + direction.z * ray_t); // [1, 2)
	uint   mip_index = 0;
	
	bool result_is_hit = false;
	uint max_iterations = 256;
	for (uint i = 0; i < max_iterations && ray_t < t_max && all(position.xy >= 1.0) && all(position.xy < 2.0); i += 1) {
		uint2 position_u32 = asuint(position.xy);
		
		uint2 volume_coordinates = ((position_u32 >> 12) & 0x7FF) >> mip_index;
		float max_height = height_field.mips[mip_index][volume_coordinates];
		bool is_hit = (position.z <= max_height);
		
		if (is_hit == false) {
			uint scale_exp = 12 + mip_index;
			float scale = asfloat((scale_exp - 23u + 127u) << 23u);
			
			float2 voxel_min = asfloat(asuint(position.xy) & (u32_max << scale_exp));
			float2 voxel_far = voxel_min + select(direction.xy >= 0.0, scale, 0.0);
			
			// @inv_direction might contain NANs, they would get rejected by min, which always returns non NAN argument.
			float2 far_intersection = (voxel_far - origin.xy) * inv_direction.xy;
			ray_t = min(far_intersection.x, far_intersection.y);
			
			if (direction.z < 0.0) {
				ray_t = min(ray_t, (max_height - (1.0 / 1024.0) - origin.z) * inv_direction.z);
			}
			
			float2 next_voxel_min = select(ray_t == far_intersection, voxel_min + select(direction.xy >= 0.0, +scale, -scale), voxel_min);
			float2 next_voxel_max = asfloat(asint(next_voxel_min) + ((1u << scale_exp) - 1));
			
			position.xy = clamp(origin.xy + direction.xy * ray_t, next_voxel_min, next_voxel_max);
			position.z  = max(origin.z    + direction.z  * ray_t, max_height);
			
			mip_index = min(mip_index + 1, 11);
		} else if (mip_index != 0) {
			mip_index -= 1;
		} else {
			// TODO: Reconstruct continuous surface.
			i = max_iterations;
			result_is_hit = true;
		}
	}
	
	return result_is_hit ? ray_t * terrain_world_space_size : -1.0;
}

float3 SampleHeightFieldNormal(float3 position) {
	float2 uv = (position.xy - terrain_world_space_position.xy) * inv_terrain_world_space_size;
	
	float delta_uv     = 1.0 / 2048.0;
	float delta_meters = delta_uv * terrain_world_space_size;
	
	float dx =
		height_field.SampleLevel(sampler_linear_clamp, uv, 0.0, s32x2(-1, 0)) -
		height_field.SampleLevel(sampler_linear_clamp, uv, 0.0, s32x2(+1, 0));
	
	float dy =
		height_field.SampleLevel(sampler_linear_clamp, uv, 0.0, s32x2(0, -1)) -
		height_field.SampleLevel(sampler_linear_clamp, uv, 0.0, s32x2(0, +1));
	
	return normalize(float3(dx, dy, 2.0 * delta_meters));
}

[ThreadGroupSize(thread_group_size * thread_group_size, 1, 1)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2  thread_id = group_id * thread_group_size + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5 - scene.jitter_offset_pixels) * scene.inv_render_target_size;
	
	RayInfo view_space_ray = RayInfoFromScreenUv(thread_uv, scene.clip_to_view_coef);
	
	RayDesc ray_desc;
	ray_desc.Origin    = mul(scene.view_to_world, float4(view_space_ray.origin, 1.0));
	ray_desc.Direction = mul((float3x3)scene.view_to_world, view_space_ray.direction);
	
	float depth = depth_stencil[thread_id];
	float ray_t_max = length(TransformScreenUvToViewSpace(thread_uv, depth, scene.clip_to_view_coef, scene.jitter_offset_ndc));
	
	BoxIntersection intersection = RayBoxIntersection(ray_desc.Origin - terrain_world_space_position, ray_desc.Direction, terrain_world_space_size, ray_t_max);
	ray_desc.TMin = intersection.t_min;
	ray_desc.TMax = intersection.t_max;
	
	float result_t = -1.0;
	if (intersection.is_hit) {
		result_t = TraceRay(ray_desc);
	}
	
	if (result_t > 0.0) {
		float3 normal = SampleHeightFieldNormal(ray_desc.Origin + ray_desc.Direction * result_t);
		
		float3 result = 0.0;
		result = normal * 0.5 + 0.5;
		// result = PlasmaHeatMap(sin(log2(result_t) * TAU) * 0.5 + 0.5);
		
		scene_radiance[thread_id] = float4(result, 1.0);
	}
}
#endif // defined(TRACE_PREVIEW)


#if defined(BUILD_PREVIEW)
#include "ParallelReduction.hlsl"

groupshared float gs_samples[64];
groupshared uint gs_thread_group_exit_index;

struct ParallelReductionSettings {
	static float Reduce(float lh, float rh) { return max(lh, rh); }
	static void StoreResult(u32 mip_index, uint2 coordinates, float sample) { height_field_mips[mip_index][coordinates] = sample; }
	static void StoreGroupShared(u32 index, float sample) { gs_samples[index] = sample; }
	static float LoadGroupShared(u32 index) { return gs_samples[index]; }
};


[ThreadGroupSize(1024, 1, 1)][WaveSize(16, 128)]
void MainCS(uint2 group_id : SV_GroupID, uint thread_index : SV_GroupIndex) {
	uint2 thread_id = group_id * 32 + MortonDecode(thread_index);
	float2 thread_uv = (thread_id + 0.5) * constants.inv_render_target_size;
	
	float4 samples = GatherChannel0(height_field, sampler_linear_clamp, thread_uv);
	float sample = GroupDownSample4096x6<ParallelReductionSettings>(thread_id, group_id, thread_index, 0, samples.x, samples.y, samples.z, samples.w);
	
	if (thread_index == 0) {
		InterlockedAdd(parallel_reduction_state[0], 1u, gs_thread_group_exit_index);
		
		uint original = 0;
		InterlockedExchange(parallel_reduction_state[MortonEncode(group_id) + 1], asuint(sample), original);
	}
	
	GroupMemoryBarrierWithGroupSync();
	
	if (gs_thread_group_exit_index != constants.last_thread_group_index) return;
	thread_id = MortonDecode(thread_index);
	
	uint sample0_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 0) + 1], 0u, sample0_uint);
	uint sample1_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 1) + 1], 0u, sample1_uint);
	uint sample2_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 2) + 1], 0u, sample2_uint);
	uint sample3_uint; InterlockedExchange(parallel_reduction_state[(thread_index * 4 + 3) + 1], 0u, sample3_uint);
	
	float sample0 = asfloat(sample0_uint);
	float sample1 = asfloat(sample1_uint);
	float sample2 = asfloat(sample2_uint);
	float sample3 = asfloat(sample3_uint);
	GroupDownSample4096x6<ParallelReductionSettings>(thread_id, 0, thread_index, 6, sample0, sample1, sample2, sample3);
	
	if (thread_index == 0) {
		uint original = 0;
		InterlockedExchange(parallel_reduction_state[0], 0u, original);
	}
}
#endif // defined(BUILD_PREVIEW)
