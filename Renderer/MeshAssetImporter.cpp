#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicThreads.h"
#include "MeshAsset.h"

#include <SDK/ufbx/ufbx.h>
#include <SDK/MeshDecimationTools/MeshDecimationTools.h>

static float3x4 LoadUfbxMatrix3x4(const ufbx_matrix& m) {
	float3x4 result;
	result.r0 = float4(m.m00, m.m01, m.m02, m.m03);
	result.r1 = float4(m.m10, m.m11, m.m12, m.m13);
	result.r2 = float4(m.m20, m.m21, m.m22, m.m23);
	return result;
}

static float3x3 LoadUfbxMatrix3x3(const ufbx_matrix& m) {
	float3x3 result;
	result.r0 = float3(m.m00, m.m01, m.m02);
	result.r1 = float3(m.m10, m.m11, m.m12);
	result.r2 = float3(m.m20, m.m21, m.m22);
	return result;
}

static MeshletErrorMetric LoadMdtErrorMetric(const MdtErrorMetric& metric) {
	MeshletErrorMetric result;
	result.center = float3(metric.bounds.center);
	result.radius = metric.bounds.radius;
	result.error  = metric.error;
	return result;
}

struct MdtHeapAllocatorUserData {
	RWLock lock;
	HeapAllocator heap;
};

struct MeshletGroupSortKey {
	u64 key = 0;
	u32 mdt_group_index = 0;
};

struct SourceMeshVertex {
	float3 position;
	float3 normal;
	float3 tangent;
	float2 texcoord;
};

MeshImportResult ImportMeshFile(StackAllocator* alloc, ThreadPool* thread_pool, const MeshSourceData& source_data, u64 runtime_data_guid) {
	ProfilerScope("ImportMeshFile");
	TempAllocationScope(alloc);
	
	auto file_data = SystemReadFileToString(alloc, source_data.filepath);
	if (file_data.data == nullptr) return {};
	
	MdtHeapAllocatorUserData allocator_user_data;
	allocator_user_data.heap = CreateHeapAllocator(64 * 1024 * 1024);
	defer{ ReleaseHeapAllocator(allocator_user_data.heap); };
	
	ufbx_load_opts options = {};
	options.ignore_animation = true;
	options.ignore_embedded  = true;
	options.target_axes      = ufbx_axes_right_handed_z_up;
	options.target_unit_meters = 1.f;
	options.generate_missing_normals = true;
	
	options.thread_opts.pool.run_fn = [](void* user_data, ufbx_thread_pool_context context, u32 group_index, u32 start_index, u32 count)-> void {
		ParallelFor((ThreadPool*)user_data, start_index, start_index + count, 1, [&](u64 work_item_index, u32 thread_index) {
			ufbx_thread_pool_run_task(context, (u32)work_item_index);
		});
	};
	options.thread_opts.pool.wait_fn = [](void* user_data, ufbx_thread_pool_context context, u32 group_index, u32 max_index)-> void {
		// ParallelFor waits by default.
	};
	options.thread_opts.pool.user = thread_pool;
	
	options.temp_allocator.allocator.alloc_fn = [](void* user_data, u64 size_bytes)-> void* {
		auto& allocator_user_data = *(MdtHeapAllocatorUserData*)user_data;
		ScopedLock(allocator_user_data.lock);
		return allocator_user_data.heap.Allocate(size_bytes);
	};
	options.temp_allocator.allocator.realloc_fn = [](void* user_data, void* old_memory_block, u64 old_size_bytes, u64 new_size_bytes)-> void* {
		auto& allocator_user_data = *(MdtHeapAllocatorUserData*)user_data;
		ScopedLock(allocator_user_data.lock);
		return allocator_user_data.heap.Reallocate(old_memory_block, old_size_bytes, new_size_bytes);
	};
	options.temp_allocator.allocator.free_fn = [](void* user_data, void* old_memory_block, u64 old_size_bytes)-> void {
		auto& allocator_user_data = *(MdtHeapAllocatorUserData*)user_data;
		ScopedLock(allocator_user_data.lock);
		allocator_user_data.heap.Deallocate(old_memory_block, old_size_bytes);
	};
	options.temp_allocator.allocator.user = &allocator_user_data;
	options.result_allocator = options.temp_allocator;
	
	ufbx_error error = {};
	auto* scene = ufbx_load_memory(file_data.data, file_data.count, &options, &error);
	if (error.type != UFBX_ERROR_NONE) return {};
	
	u32 max_face_triangles = 0;
	u32 max_mesh_triangles = 0;
	u32 max_result_triangles = 0;
	for (auto* mesh : scene->meshes) {
		max_face_triangles = Math::Max(max_face_triangles, (u32)mesh->max_face_triangles);
		max_mesh_triangles = Math::Max(max_mesh_triangles, (u32)mesh->num_triangles);
		max_result_triangles += (u32)(mesh->num_triangles * mesh->instances.count);
	}
	
	Array<SourceMeshVertex> source_vertices;
	Array<u32> source_indices;
	ArrayReserve(source_vertices, alloc, max_result_triangles * 3);
	ArrayReserve(source_indices,  alloc, max_result_triangles * 3);
	
	Array<u32> face_indices;
	ArrayReserve(face_indices, alloc, max_face_triangles * 3);
	
	Array<SourceMeshVertex> mesh_vertices;
	ArrayReserve(mesh_vertices, alloc, max_mesh_triangles * 3);
	
	Array<u32> mesh_indices;
	ArrayResize(mesh_indices, alloc, max_mesh_triangles * 3);
	
	for (auto* mesh : scene->meshes) {
		mesh_vertices.count = 0;
		
		for (auto face : mesh->faces) {
			face_indices.count = ufbx_triangulate_face(face_indices.data, face_indices.capacity, mesh, face) * 3;
			
			for (u32 index : face_indices) {
				SourceMeshVertex vertex;
				vertex.position = float3(mesh->vertex_position[index]);
				vertex.normal   = float3(mesh->vertex_normal[index]);
				vertex.tangent  = mesh->vertex_tangent.exists ? float3(mesh->vertex_tangent[index]) : vertex.normal; // Fallback to normal so we have at least some sort of a valid vector.
				vertex.texcoord = mesh->vertex_uv.exists ? float2(mesh->vertex_uv[index].x, 1.f - mesh->vertex_uv[index].y) : float2(0.f, 0.f);
				ArrayAppend(mesh_vertices, vertex);
			}
		}
		
		ufbx_vertex_stream stream;
		stream.data         = mesh_vertices.data;
		stream.vertex_count = mesh_vertices.count;
		stream.vertex_size  = sizeof(SourceMeshVertex);
		
		mesh_indices.count  = mesh_vertices.count;
		mesh_vertices.count = ufbx_generate_indices(&stream, 1, mesh_indices.data, mesh_indices.count, &options.temp_allocator, nullptr);
		
		for (auto* instance : mesh->instances) {
			auto geometry_to_world          = LoadUfbxMatrix3x4(instance->geometry_to_world);
			auto geometry_to_world_rotation = LoadUfbxMatrix3x3(ufbx_matrix_for_normals(&instance->geometry_to_world));
			
			u32 index_offset = (u32)source_vertices.count;
			for (auto& vertex : mesh_vertices) {
				auto instance_vertex = vertex;
				instance_vertex.position = geometry_to_world * float4(instance_vertex.position, 1.f);
				instance_vertex.normal   = Math::Normalize(geometry_to_world_rotation * instance_vertex.normal);
				instance_vertex.tangent  = Math::Normalize(geometry_to_world_rotation * instance_vertex.tangent);
				ArrayAppend(source_vertices, instance_vertex);
			}
			
			for (u32 index : mesh_indices) {
				ArrayAppend(source_indices, index + index_offset);
			}
		}
	}
	ufbx_free_scene(scene);
	
	MdtSystemCallbacks callbacks = {};
	callbacks.heap_allocator.reallocate = [](void* old_memory_block, u64 size_bytes, void* user_data)-> void* {
		auto& allocator_user_data = *(MdtHeapAllocatorUserData*)user_data;
		u64 old_size_bytes = HeapAllocator::GetMemoryBlockSize(old_memory_block);
		
		ScopedLock(allocator_user_data.lock);
		return allocator_user_data.heap.Reallocate(old_memory_block, old_size_bytes, size_bytes, 64);
	};
	callbacks.heap_allocator.user_data = &allocator_user_data;
	
	
	callbacks.parallel_for.callback = [](void* user_data, void* mdt_data, u32 work_item_count, MdtWorkItemCallback callback)-> void {
		ParallelFor((ThreadPool*)user_data, 0, work_item_count, 1, [&](u64 work_item_index, u32 thread_index) {
			callback(mdt_data, (u32)work_item_index);
		});
	};
	callbacks.parallel_for.user_data = thread_pool;
	callbacks.parallel_for.thread_count = 32;
	
	
	MdtTriangleGeometryDesc geometry_descs[1] = {};
	geometry_descs[0].indices      = source_indices.data;
	geometry_descs[0].vertices     = (float*)source_vertices.data;
	geometry_descs[0].index_count  = (u32)source_indices.count;
	geometry_descs[0].vertex_count = (u32)source_vertices.count;
	
	float attribute_weights[MDT_MAX_ATTRIBUTE_STRIDE_DWORDS] = {};
	attribute_weights[0] = attribute_weights[1] = attribute_weights[2] = 0.25f; // Normal
	attribute_weights[3] = attribute_weights[4] = attribute_weights[5] = 0.05f; // Tangent
	attribute_weights[6] = attribute_weights[7] = 0.25f;                        // Texcoord
	
	MdtContinuousLodBuildInputs inputs = {};
	inputs.mesh.geometry_descs      = geometry_descs;
	inputs.mesh.geometry_desc_count = 1;
	inputs.mesh.vertex_stride_bytes = sizeof(SourceMeshVertex);
	inputs.mesh.geometric_weight    = 0.f;
	inputs.mesh.attribute_weights   = attribute_weights;
	inputs.mesh.normalize_vertex_attributes = [](float* attributes) {
		auto normal  = *(float3*)(attributes + 0);
		auto tangent = *(float3*)(attributes + 3);
		
		float normal_length  = Math::Length(normal);
		float tangent_length = Math::Length(tangent);
		
		if (normal_length  > 0.f) normal  = normal  * (1.f / normal_length);
		if (tangent_length > 0.f) tangent = tangent * (1.f / tangent_length);
	};
	
	compile_const u32 max_triangles_per_meshlet = 128;
	compile_const u32 max_vertices_per_meshlet  = 128;
	
	inputs.meshlet_target_triangle_count = max_triangles_per_meshlet;
	inputs.meshlet_target_vertex_count   = max_vertices_per_meshlet;
	
	MdtContinuousLodBuildResult result = {};
	MdtBuildContinuousLod(&inputs, &result, &callbacks);
	
	auto mdt_meshlet_groups         = ArrayView<MdtMeshletGroup>{ result.meshlet_groups, result.meshlet_group_count };
	auto mdt_meshlets               = ArrayView<MdtMeshlet>{ result.meshlets, result.meshlet_count };
	auto mdt_meshlet_vertex_indices = ArrayView<u32>{ result.meshlet_vertex_indices, result.meshlet_vertex_index_count };
	auto mdt_meshlet_triangles      = ArrayView<MdtMeshletTriangle>{ result.meshlet_triangles, result.meshlet_triangle_count };
	auto mdt_meshlet_vertices       = ArrayView<SourceMeshVertex>{ (SourceMeshVertex*)result.vertices, result.vertex_count };
	auto mdt_levels_of_detail       = ArrayView<MdtContinuousLodLevel>{ result.levels, result.level_count };
	
	auto result_meshlet_groups = ArrayViewAllocate<MeshletGroup>(alloc, mdt_meshlet_groups.count);
	
	auto mesh_aabb_min = float3(+FLT_MAX, +FLT_MAX, +FLT_MAX);
	auto mesh_aabb_max = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (u32 i = 0; i < mdt_meshlet_groups.count; i += 1) {
		auto& src_group = mdt_meshlet_groups[i];
		mesh_aabb_min = Math::Min(mesh_aabb_min, float3(src_group.aabb_min));
		mesh_aabb_max = Math::Max(mesh_aabb_max, float3(src_group.aabb_max));
	}
	auto mesh_to_unit_scale = float3(1.f) / Math::Max(mesh_aabb_max - mesh_aabb_min, float3(1.f));
	
	auto meshlet_group_sort_keys = ArrayViewAllocate<MeshletGroupSortKey>(alloc, mdt_meshlet_groups.count);
	for (u32 i = 0; i < mdt_meshlet_groups.count; i += 1) {
		auto& src_group = mdt_meshlet_groups[i];
		auto& dst_group = meshlet_group_sort_keys[i];
		
		auto center = (float3(src_group.aabb_min) + float3(src_group.aabb_max)) * 0.5f;
		auto center_01  = (center - mesh_aabb_min) * mesh_to_unit_scale;
		auto center_u32 = uint3(center_01 * 0xFFFFFu);
		
		// Sort by 4 bit LOD first, then spatially by 60 bit morton code of the AABB center.
		u64 key = (u64)(MDT_MAX_CLOD_LEVEL_COUNT - 1 - src_group.level_of_detail_index) << 60;
		key |= DepositBits(center_u32.x, 0x0249249249249249);
		key |= DepositBits(center_u32.y, 0x0492492492492492);
		key |= DepositBits(center_u32.z, 0x0924924924924924);
		
		dst_group.key = key;
		dst_group.mdt_group_index = i;
	}
	
	HeapSort(meshlet_group_sort_keys, [](auto& lh, auto& rh)-> bool {
		return lh.key < rh.key;
	});
	
	
	auto mdt_to_result_meshlet_group_index = ArrayViewAllocate<u32>(alloc, meshlet_group_sort_keys.count);
	auto result_to_mdt_meshlet_group_index = ArrayViewAllocate<u32>(alloc, meshlet_group_sort_keys.count);
	for (u32 result_group_index = 0; result_group_index < meshlet_group_sort_keys.count; result_group_index += 1) {
		u32 mdt_group_index = meshlet_group_sort_keys[result_group_index].mdt_group_index;
		
		mdt_to_result_meshlet_group_index[mdt_group_index] = result_group_index;
		result_to_mdt_meshlet_group_index[result_group_index] = mdt_group_index;
	}
	
	
	compile_const u32 page_size = MeshletPageHeader::page_size;
	
	Array<u32> page_meshlet_indices;
	Array<u32> page_meshlet_prefix_sum;
	
	u32 page_offset = sizeof(MeshletPageHeader);
	u32 page_meshlet_count = 0;
	for (u32 meshlet_group_index = 0; meshlet_group_index < mdt_meshlet_groups.count; meshlet_group_index += 1) {
		auto& src_group = mdt_meshlet_groups[result_to_mdt_meshlet_group_index[meshlet_group_index]];
		auto& dst_group = result_meshlet_groups[meshlet_group_index];
		
		dst_group.meshlet_count  = src_group.end_meshlet_index - src_group.begin_meshlet_index;
		dst_group.error_metric   = LoadMdtErrorMetric(src_group.error_metric);
		dst_group.aabb_center    = (float3(src_group.aabb_max) + float3(src_group.aabb_min)) * 0.5f;
		dst_group.aabb_radius    = (float3(src_group.aabb_max) - float3(src_group.aabb_min)) * 0.5f;
		dst_group.residency_mask = 0;
		
		for (u32 meshlet_index = src_group.begin_meshlet_index; meshlet_index < src_group.end_meshlet_index; meshlet_index += 1) {
			auto& src_meshlet = mdt_meshlets[meshlet_index];
			
			u32 triangle_count = src_meshlet.end_meshlet_triangles_index - src_meshlet.begin_meshlet_triangles_index;
			u32 vertex_count   = src_meshlet.end_vertex_indices_index    - src_meshlet.begin_vertex_indices_index;
			
			u32 meshlet_size = (u32)(sizeof(MeshletCullingData) + sizeof(MeshletHeader) + AlignUp(vertex_count * sizeof(MeshletVertex) + triangle_count * sizeof(MdtMeshletTriangle), 4));
			DebugAssert(meshlet_size <= page_size, "Meshlet is larger than a single page. (%/%).", meshlet_size, page_size);
			
			if (page_offset + meshlet_size > page_size) {
				ArrayAppend(page_meshlet_prefix_sum, alloc, (u32)page_meshlet_indices.count);
				page_offset = sizeof(MeshletPageHeader);
				page_meshlet_count = 0;
			}
			
			if (meshlet_index == src_group.begin_meshlet_index) {
				dst_group.meshlet_offset = page_meshlet_count;
				dst_group.page_index     = (u32)page_meshlet_prefix_sum.count;
			}
			
			ArrayAppend(page_meshlet_indices, alloc, meshlet_index);
			page_offset += meshlet_size;
			page_meshlet_count += 1;
		}
		
		dst_group.page_count = ((u32)page_meshlet_prefix_sum.count + 1 - dst_group.page_index);
	}
	
	if (page_meshlet_count != 0) {
		ArrayAppend(page_meshlet_prefix_sum, alloc, (u32)page_meshlet_indices.count);
	}
	
	// TODO: This is quite an expensive way to compute world_to_uv_scale, maybe we could use a histogram instead of sorting?
	auto meshlet_world_to_uv_scale = ArrayViewAllocate<float>(alloc, mdt_meshlets.count);
	ParallelFor(thread_pool, 0, mdt_meshlets.count, 32, [&](u64 meshlet_index, u32 thread_index) {
		ProfilerScope("ComputeMeshletUvPerMeter");
		
		auto& src_meshlet = mdt_meshlets[meshlet_index];
		
		FixedCapacityArray<float, max_triangles_per_meshlet * 3> edge_world_to_uv_scale;
		for (u32 i = src_meshlet.begin_meshlet_triangles_index; i < src_meshlet.end_meshlet_triangles_index; i += 1) {
			auto triangle = mdt_meshlet_triangles[i];
			
			u32 i0 = mdt_meshlet_vertex_indices[src_meshlet.begin_vertex_indices_index + triangle.i0];
			u32 i1 = mdt_meshlet_vertex_indices[src_meshlet.begin_vertex_indices_index + triangle.i1];
			u32 i2 = mdt_meshlet_vertex_indices[src_meshlet.begin_vertex_indices_index + triangle.i2];
			
			auto& v0 = mdt_meshlet_vertices[i0];
			auto& v1 = mdt_meshlet_vertices[i1];
			auto& v2 = mdt_meshlet_vertices[i2];
			
			auto position_delta_e0 = Math::Length(v1.position - v0.position);
			auto position_delta_e1 = Math::Length(v2.position - v1.position);
			auto position_delta_e2 = Math::Length(v0.position - v2.position);
			
			auto texcoord_delta_e0 = Math::Length(v1.texcoord - v0.texcoord);
			auto texcoord_delta_e1 = Math::Length(v2.texcoord - v1.texcoord);
			auto texcoord_delta_e2 = Math::Length(v0.texcoord - v2.texcoord);
			
			if (position_delta_e0 > FLT_MIN) ArrayAppend(edge_world_to_uv_scale, texcoord_delta_e0 / position_delta_e0);
			if (position_delta_e1 > FLT_MIN) ArrayAppend(edge_world_to_uv_scale, texcoord_delta_e1 / position_delta_e1);
			if (position_delta_e2 > FLT_MIN) ArrayAppend(edge_world_to_uv_scale, texcoord_delta_e2 / position_delta_e2);
		}
		
		HeapSort<float>(edge_world_to_uv_scale);
		
		// Compute median of the upper 50% of samples.
		float median = edge_world_to_uv_scale.count ? edge_world_to_uv_scale[edge_world_to_uv_scale.count * 3 / 4] : 1.f;
		
		meshlet_world_to_uv_scale[meshlet_index] = median;
	});
	
	float max_meshlet_extent = 0.f;
	for (auto& meshlet : mdt_meshlets) {
		auto extent = float3(meshlet.aabb_max) - float3(meshlet.aabb_min);
		max_meshlet_extent = Math::Max(Math::Max(extent.x, extent.y), Math::Max(extent.z, max_meshlet_extent));
	}
	
	float quantization_scale = 1024.f;
	float bit_count = max_meshlet_extent > 0.f ? ceilf(log2f(max_meshlet_extent * quantization_scale + 1.f)) : 0.f;
	if (bit_count > 16.f) {
		quantization_scale = exp2f(floorf(log2f((float)u16_max / max_meshlet_extent)));
	}
	float rcp_quantization_scale = 1.f / quantization_scale;
	
	
	Array<ArrayView<u8>> result_pages;
	ArrayReserve(result_pages, alloc, page_meshlet_prefix_sum.count);
	
	for (u32 page_index = 0, begin_meshlet_index = 0; page_index < page_meshlet_prefix_sum.count; page_index += 1) {
		auto page = ArrayViewAllocate<u8>(alloc, page_size);
		ArrayAppend(result_pages, page);
		
		u32 end_meshlet_index = page_meshlet_prefix_sum[page_index];
		
		u32 meshlet_count        = (end_meshlet_index - begin_meshlet_index);
		u32 total_triangle_count = 0;
		u32 total_vertex_count   = 0;
		
		u64 page_culling_data_offset = sizeof(MeshletPageHeader);
		u64 page_meshlet_data_offset = page_culling_data_offset + meshlet_count * sizeof(MeshletCullingData);
		
		for (u32 page_meshlet_index = begin_meshlet_index; page_meshlet_index < end_meshlet_index; page_meshlet_index += 1) {
			u32 meshlet_index = page_meshlet_indices[page_meshlet_index];
			auto& src_meshlet = mdt_meshlets[meshlet_index];
			
			u32 triangle_count = src_meshlet.end_meshlet_triangles_index - src_meshlet.begin_meshlet_triangles_index;
			u32 vertex_count   = src_meshlet.end_vertex_indices_index    - src_meshlet.begin_vertex_indices_index;
			
			total_triangle_count += triangle_count;
			total_vertex_count   += vertex_count;
			
			float3 position_offset;
			position_offset.x = floorf(src_meshlet.aabb_min.x * quantization_scale) * rcp_quantization_scale;
			position_offset.y = floorf(src_meshlet.aabb_min.y * quantization_scale) * rcp_quantization_scale;
			position_offset.z = floorf(src_meshlet.aabb_min.z * quantization_scale) * rcp_quantization_scale;
			
			u32 level_of_detail_index = mdt_meshlet_groups[src_meshlet.coarser_level_meshlet_group_index].level_of_detail_index;
			
			// Write culling data:
			{
				MeshletCullingData culling_data;
				culling_data.current_level_error_metric = LoadMdtErrorMetric(src_meshlet.current_level_error_metric);
				culling_data.meshlet_header_offset      = (u32)(page_meshlet_data_offset - page_culling_data_offset);
				culling_data.aabb_center                = (float3(src_meshlet.aabb_max) + float3(src_meshlet.aabb_min)) * 0.5f;
				culling_data.aabb_radius                = (float3(src_meshlet.aabb_max) - float3(src_meshlet.aabb_min)) * 0.5f;
				culling_data.level_of_detail_index      = level_of_detail_index;
				culling_data.world_to_uv_scale          = meshlet_world_to_uv_scale[meshlet_index];
				
				if (src_meshlet.current_level_meshlet_group_index != u32_max) {
					culling_data.current_level_meshlet_group_index = mdt_to_result_meshlet_group_index[src_meshlet.current_level_meshlet_group_index];
				}
				
				memcpy(page.data + page_culling_data_offset, &culling_data, sizeof(culling_data));
				page_culling_data_offset += sizeof(culling_data);
			}
			
			// Write header:
			{
				MeshletHeader meshlet_header;
				meshlet_header.triangle_count  = triangle_count;
				meshlet_header.vertex_count    = vertex_count;
				meshlet_header.position_offset = position_offset;
				meshlet_header.level_of_detail_index = level_of_detail_index;
				meshlet_header.rtas_offset     = u32_max;
				
				memcpy(page.data + page_meshlet_data_offset, &meshlet_header, sizeof(meshlet_header));
				page_meshlet_data_offset += sizeof(meshlet_header);
			}
			
			// Write vertices:
			for (u32 i = src_meshlet.begin_vertex_indices_index; i < src_meshlet.end_vertex_indices_index; i += 1) {
				auto& src_vertex = mdt_meshlet_vertices[mdt_meshlet_vertex_indices[i]];
				
				auto octahedral_normal  = Math::EncodeOctahedralMap(src_vertex.normal);
				auto octahedral_tangent = Math::EncodeOctahedralMap(src_vertex.tangent);
				
				MeshletVertex dst_vertex; 
				dst_vertex.position = u16x3((src_vertex.position - position_offset) * quantization_scale);
				dst_vertex.normal   = Math::EncodeR16G16_SNORM(octahedral_normal);
				dst_vertex.tangent  = Math::EncodeR16G16_SNORM(octahedral_tangent); // TODO: Store tangent as an angle relative to a canonical tangent.
				dst_vertex.texcoord = Math::EncodeR16G16_FLOAT(src_vertex.texcoord);
				
				memcpy(page.data + page_meshlet_data_offset, &dst_vertex, sizeof(dst_vertex));
				page_meshlet_data_offset += sizeof(dst_vertex);
			}
			
			// Write indices:
			{
				memcpy(page.data + page_meshlet_data_offset, mdt_meshlet_triangles.data + src_meshlet.begin_meshlet_triangles_index, triangle_count * sizeof(MdtMeshletTriangle));
				page_meshlet_data_offset += triangle_count * sizeof(MdtMeshletTriangle);
			}
			
			// Write page header:
			{
				MeshletPageHeader page_header;
				page_header.meshlet_count        = meshlet_count;
				page_header.total_triangle_count = total_triangle_count;
				page_header.total_vertex_count   = total_vertex_count;
				
				memcpy(page.data, &page_header, sizeof(page_header));
			}
			
			// Clear padding to zero:
			memset(page.data + page_meshlet_data_offset, 0, AlignUp(page_meshlet_data_offset, 4) - page_meshlet_data_offset);
			page_meshlet_data_offset = AlignUp(page_meshlet_data_offset, 4);
		}
		
		// Clear padding to zero:
		memset(page.data + page_meshlet_data_offset, 0, page_size - page_meshlet_data_offset);
		
		begin_meshlet_index = end_meshlet_index;
	}
	
	MdtFreeContinuousLodBuildResult(&result, &callbacks);
	
	auto runtime_filepath = StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, runtime_data_guid);
	
	auto runtime_file = SystemOpenFile(alloc, runtime_filepath, OpenFileFlags::Write);
	if (runtime_file.handle == nullptr) return {};
	bool write_file_success = true;
	
	MeshRuntimeDataLayout runtime_data_layout;
	runtime_data_layout.file_guid              = runtime_data_guid;
	runtime_data_layout.version                = MeshRuntimeDataLayout::current_version;
	runtime_data_layout.page_count             = (u32)result_pages.count;
	runtime_data_layout.meshlet_group_count    = (u32)result_meshlet_groups.count;
	runtime_data_layout.meshlet_count          = (u32)mdt_meshlets.count;
	runtime_data_layout.rcp_quantization_scale = rcp_quantization_scale;
	
	u64 write_offset = 0;
	for (auto page : result_pages) {
		write_file_success &= SystemWriteFile(runtime_file, page.data, page_size, write_offset);
		write_offset += page_size;
	}
	
	write_file_success &= SystemWriteFile(runtime_file, result_meshlet_groups.data, result_meshlet_groups.count * sizeof(MeshletGroup), write_offset);
	write_offset += result_meshlet_groups.count * sizeof(MeshletGroup);
	
	Array<u32> page_and_rtas_residency_masks;
	ArrayResizeMemset(page_and_rtas_residency_masks, alloc, DivideAndRoundUp(result_pages.count, sizeof(u32)) * 2);
	write_file_success &= SystemWriteFile(runtime_file, page_and_rtas_residency_masks.data, page_and_rtas_residency_masks.count * sizeof(u32), write_offset);
	write_offset += page_and_rtas_residency_masks.count * sizeof(u32);
	
	Array<u32> page_table;
	ArrayResizeMemset(page_table, alloc, result_pages.count);
	write_file_success &= SystemWriteFile(runtime_file, page_table.data, page_table.count * sizeof(u32), write_offset);
	write_offset += page_table.count * sizeof(u32);
	
	write_file_success &= SystemCloseFile(runtime_file);
	
	return { runtime_data_layout, mesh_aabb_min, mesh_aabb_max, write_file_success };
}
