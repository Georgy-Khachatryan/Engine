#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMath.h"
#include "MeshAsset.h"

#include <SDK/ufbx/ufbx.h>
#include <SDK/MeshDecimationTools/MeshDecimationTools.h>

static float3x4 LoadUfbxMatrix(const ufbx_matrix& m) {
	float3x4 result;
	result.r0 = float4(m.m00, m.m01, m.m02, m.m03);
	result.r1 = float4(m.m10, m.m11, m.m12, m.m13);
	result.r2 = float4(m.m20, m.m21, m.m22, m.m23);
	return result;
}

static MeshletErrorMetric LoadMdtErrorMetric(const MdtErrorMetric& metric) {
	MeshletErrorMetric result;
	result.center = float3(metric.bounds.center);
	result.radius = metric.bounds.radius;
	result.error  = metric.error;
	return result;
}

struct MeshletGroupSortKey {
	u64 key = 0;
	u32 mdt_group_index = 0;
};

MeshRuntimeDataLayout ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid) {
	ProfilerScope("ImportFbxMeshFile");
	TempAllocationScope(alloc);
	
	auto file_data = SystemReadFileToString(alloc, filepath);
	DebugAssert(file_data.data != nullptr, "Failed to load mesh file '%'", filepath);
	
	ufbx_load_opts options = {};
	options.ignore_animation = true;
	options.ignore_embedded  = true;
	options.target_axes      = ufbx_axes_right_handed_z_up;
	options.target_unit_meters = 1.f;
	options.generate_missing_normals = true;
	
	ufbx_error error = {};
	auto* scene = ufbx_load_memory(file_data.data, file_data.count, &options, &error);
	
	u32 max_face_triangles = 0;
	u32 max_mesh_triangles = 0;
	u32 max_result_triangles = 0;
	for (auto* mesh : scene->meshes) {
		max_face_triangles = Math::Max(max_face_triangles, (u32)mesh->max_face_triangles);
		max_mesh_triangles = Math::Max(max_mesh_triangles, (u32)mesh->num_triangles);
		max_result_triangles += (u32)(mesh->num_triangles * mesh->instances.count);
	}
	
	Array<BasicVertex> source_vertices;
	Array<u32> source_indices;
	ArrayReserve(source_vertices, alloc, max_result_triangles * 3);
	ArrayReserve(source_indices,  alloc, max_result_triangles * 3);
	
	Array<u32> face_indices;
	ArrayReserve(face_indices, alloc, max_face_triangles * 3);
	
	Array<BasicVertex> mesh_vertices;
	ArrayReserve(mesh_vertices, alloc, max_mesh_triangles * 3);
	
	Array<u32> mesh_indices;
	ArrayResize(mesh_indices, alloc, max_mesh_triangles * 3);
	
	for (auto* mesh : scene->meshes) {
		mesh_vertices.count = 0;
		
		for (auto face : mesh->faces) {
			face_indices.count = ufbx_triangulate_face(face_indices.data, face_indices.capacity, mesh, face) * 3;
			
			for (u32 index : face_indices) {
				BasicVertex vertex;
				vertex.position = float3(mesh->vertex_position[index]);
				vertex.normal   = float3(mesh->vertex_normal[index]);
				vertex.texcoord = mesh->vertex_uv.exists ? float2(mesh->vertex_uv[index]) : float2(0.f, 0.f);
				ArrayAppend(mesh_vertices, vertex);
			}
		}
		
		ufbx_vertex_stream stream;
		stream.data         = mesh_vertices.data;
		stream.vertex_count = mesh_vertices.count;
		stream.vertex_size  = sizeof(BasicVertex);
		
		mesh_indices.count  = mesh_vertices.count;
		mesh_vertices.count = ufbx_generate_indices(&stream, 1, mesh_indices.data, mesh_indices.count, nullptr, nullptr);
		
		for (auto* instance : mesh->instances) {
			auto geometry_to_world = LoadUfbxMatrix(instance->geometry_to_world);
			
			u32 index_offset = (u32)source_vertices.count;
			for (auto& vertex : mesh_vertices) {
				auto instance_vertex = vertex;
				instance_vertex.position = geometry_to_world * float4(instance_vertex.position, 1.f);
				ArrayAppend(source_vertices, instance_vertex);
			}
			
			for (u32 index : mesh_indices) {
				ArrayAppend(source_indices, index + index_offset);
			}
		}
	}
	
	
	MdtSystemCallbacks callbacks = {};
	callbacks.temp_allocator.reallocate = [](void* old_memory_block, u64 size_bytes, void* user_data)-> void* {
		auto* alloc = (StackAllocator*)user_data;
		return alloc->Reallocate(old_memory_block, 0, size_bytes, 16);
	};
	callbacks.temp_allocator.user_data = alloc;
	
	callbacks.heap_allocator.reallocate = [](void* old_memory_block, u64 size_bytes, void*) {
		void* result = nullptr;
		if (size_bytes == 0) {
			free(old_memory_block);
		} else {
			result = realloc(old_memory_block, size_bytes);
		}
		return result;
	};
	callbacks.heap_allocator.user_data = nullptr;
	
	
	MdtTriangleGeometryDesc geometry_descs[1] = {};
	geometry_descs[0].indices      = source_indices.data;
	geometry_descs[0].vertices     = (float*)source_vertices.data;
	geometry_descs[0].index_count  = (u32)source_indices.count;
	geometry_descs[0].vertex_count = (u32)source_vertices.count;
	
	MdtContinuousLodBuildInputs inputs = {};
	inputs.mesh.geometry_descs      = geometry_descs;
	inputs.mesh.geometry_desc_count = 1;
	inputs.mesh.vertex_stride_bytes = sizeof(BasicVertex);
	inputs.mesh.geometric_weight    = 0.f;
	inputs.mesh.attribute_weights   = nullptr;
	inputs.mesh.normalize_vertex_attributes = [](float* attributes) {
		auto normal = *(float3*)attributes;
		float length = Math::Length(normal);
		if (length > 0.f) normal = normal * (1.f / length);
	};
	
	inputs.meshlet_target_triangle_count = 128;
	inputs.meshlet_target_vertex_count   = 128;
	
	MdtContinuousLodBuildResult result = {};
	MdtBuildContinuousLod(&inputs, &result, &callbacks);
	
	auto mdt_meshlet_groups         = ArrayView<MdtMeshletGroup>{ result.meshlet_groups, result.meshlet_group_count };
	auto mdt_meshlets               = ArrayView<MdtMeshlet>{ result.meshlets, result.meshlet_count };
	auto mdt_meshlet_vertex_indices = ArrayView<u32>{ result.meshlet_vertex_indices, result.meshlet_vertex_index_count };
	auto mdt_meshlet_triangles      = ArrayView<MdtMeshletTriangle>{ result.meshlet_triangles, result.meshlet_triangle_count };
	auto mdt_meshlet_vertices       = ArrayView<BasicVertex>{ (BasicVertex*)result.vertices, result.vertex_count };
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
		
		dst_group.meshlet_count = src_group.end_meshlet_index - src_group.begin_meshlet_index;
		dst_group.error_metric  = LoadMdtErrorMetric(src_group.error_metric);
		
		for (u32 meshlet_index = src_group.begin_meshlet_index; meshlet_index < src_group.end_meshlet_index; meshlet_index += 1) {
			auto& src_meshlet = mdt_meshlets[meshlet_index];
			
			u32 triangle_count = src_meshlet.end_meshlet_triangles_index - src_meshlet.begin_meshlet_triangles_index;
			u32 vertex_count   = src_meshlet.end_vertex_indices_index    - src_meshlet.begin_vertex_indices_index;
			
			u32 meshlet_size = (u32)(sizeof(MeshletCullingData) + sizeof(MeshletHeader) + vertex_count * sizeof(BasicVertex) + AlignUp(triangle_count * sizeof(MdtMeshletTriangle), 4));
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
	
	
	Array<ArrayView<u8>> result_pages;
	ArrayReserve(result_pages, alloc, page_meshlet_prefix_sum.count);
	
	for (u32 page_index = 0, begin_meshlet_index = 0; page_index < page_meshlet_prefix_sum.count; page_index += 1) {
		auto page = ArrayViewAllocate<u8>(alloc, page_size);
		ArrayAppend(result_pages, page);
		
		u32 end_meshlet_index = page_meshlet_prefix_sum[page_index];
		u32 meshlet_count = (end_meshlet_index - begin_meshlet_index);
		
		// Write page header:
		{
			MeshletPageHeader page_header;
			page_header.meshlet_count = meshlet_count;
			
			memcpy(page.data, &page_header, sizeof(page_header));
		}
		
		u64 page_culling_data_offset = sizeof(MeshletPageHeader);
		u64 page_meshlet_data_offset = page_culling_data_offset + meshlet_count * sizeof(MeshletCullingData);
		
		for (u32 page_meshlet_index = begin_meshlet_index; page_meshlet_index < end_meshlet_index; page_meshlet_index += 1) {
			u32 meshlet_index = page_meshlet_indices[page_meshlet_index];
			auto& src_meshlet = mdt_meshlets[meshlet_index];
			
			u32 triangle_count = src_meshlet.end_meshlet_triangles_index - src_meshlet.begin_meshlet_triangles_index;
			u32 vertex_count   = src_meshlet.end_vertex_indices_index    - src_meshlet.begin_vertex_indices_index;
			
			// Write culling data:
			{
				MeshletCullingData culling_data;
				culling_data.current_level_error_metric = LoadMdtErrorMetric(src_meshlet.current_level_error_metric);
				culling_data.meshlet_header_offset      = (u32)(page_meshlet_data_offset - page_culling_data_offset);
				
				if (src_meshlet.current_level_meshlet_group_index != u32_max) {
					culling_data.current_level_meshlet_group_index = mdt_to_result_meshlet_group_index[src_meshlet.current_level_meshlet_group_index];
				}
				
				memcpy(page.data + page_culling_data_offset, &culling_data, sizeof(culling_data));
				page_culling_data_offset += sizeof(culling_data);
			}
			
			// Write header:
			{
				MeshletHeader meshlet_header;
				meshlet_header.triangle_count = triangle_count;
				meshlet_header.vertex_count   = vertex_count;
				
				memcpy(page.data + page_meshlet_data_offset, &meshlet_header, sizeof(meshlet_header));
				page_meshlet_data_offset += sizeof(meshlet_header);
			}
			
			// Write vertices:
			for (u32 i = src_meshlet.begin_vertex_indices_index; i < src_meshlet.end_vertex_indices_index; i += 1) {
				auto& vertex = mdt_meshlet_vertices[mdt_meshlet_vertex_indices[i]];
				memcpy(page.data + page_meshlet_data_offset, &vertex, sizeof(vertex));
				page_meshlet_data_offset += sizeof(vertex);
			}
			
			// Write indices:
			{
				memcpy(page.data + page_meshlet_data_offset, mdt_meshlet_triangles.data + src_meshlet.begin_meshlet_triangles_index, triangle_count * sizeof(MdtMeshletTriangle));
				page_meshlet_data_offset += AlignUp(triangle_count * sizeof(MdtMeshletTriangle), 4);
			}
		}
		
		// Clear padding to zero:
		memset(page.data + page_meshlet_data_offset, 0, page_size - page_meshlet_data_offset);
		
		begin_meshlet_index = end_meshlet_index;
	}
	
	MdtFreeContinuousLodBuildResult(&result, &callbacks);
	
	
	auto runtime_filepath = StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, runtime_data_guid);
	
	auto runtime_file = SystemOpenFile(alloc, runtime_filepath, OpenFileFlags::Write);
	DebugAssert(runtime_file.handle, "Failed to open mesh output file.");
	defer{ SystemCloseFile(runtime_file); };
	
	MeshRuntimeDataLayout runtime_data_layout;
	runtime_data_layout.file_guid  = runtime_data_guid;
	runtime_data_layout.version    = MeshRuntimeDataLayout::current_version;
	runtime_data_layout.page_count = (u32)result_pages.count;
	runtime_data_layout.meshlet_group_count = (u32)result_meshlet_groups.count;
	
	u64 write_offset = 0;
	for (auto page : result_pages) {
		SystemWriteFile(runtime_file, page.data, page_size, write_offset);
		write_offset += page_size;
	}
	
	SystemWriteFile(runtime_file, result_meshlet_groups.data, result_meshlet_groups.count * sizeof(MeshletGroup), write_offset);
	write_offset += result_meshlet_groups.count * sizeof(MeshletGroup);
	
	u32 page_residency_mask[MeshletPageHeader::max_page_count / 32u] = {};
	SystemWriteFile(runtime_file, &page_residency_mask, sizeof(page_residency_mask), write_offset);
	write_offset += sizeof(page_residency_mask);
	
	return runtime_data_layout;
}
