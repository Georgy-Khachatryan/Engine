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

MeshRuntimeDataLayout ImportFbxMeshFile(StackAllocator* alloc, String filepath, u64 runtime_data_guid) {
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
		max_face_triangles = Max(max_face_triangles, (u32)mesh->max_face_triangles);
		max_mesh_triangles = Max(max_mesh_triangles, (u32)mesh->num_triangles);
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
	
	auto result_vertices = ArrayViewAllocate<BasicVertex>(alloc, mdt_meshlet_vertex_indices.count);
	auto result_meshlets = ArrayViewAllocate<BasicMeshlet>(alloc, mdt_meshlets.count);
	auto result_indices  = ArrayViewAllocate<u8>(alloc, mdt_meshlet_triangles.count * 3);
	
	memcpy(result_indices.data, mdt_meshlet_triangles.data, mdt_meshlet_triangles.count * sizeof(MdtMeshletTriangle));
	static_assert(sizeof(MdtMeshletTriangle) == 3);
	
	for (u32 i = 0; i < mdt_meshlet_vertex_indices.count; i += 1) {
		result_vertices[i] = mdt_meshlet_vertices[mdt_meshlet_vertex_indices[i]];
	}
	
	for (u32 i = 0; i < mdt_meshlets.count; i += 1) {
		auto& src_meshlet = mdt_meshlets[i];
		auto& dst_meshlet = result_meshlets[i];
		
		dst_meshlet.index_buffer_offset  = src_meshlet.begin_meshlet_triangles_index * 3;
		dst_meshlet.vertex_buffer_offset = src_meshlet.begin_vertex_indices_index;
		dst_meshlet.triangle_count       = src_meshlet.end_meshlet_triangles_index - src_meshlet.begin_meshlet_triangles_index;
		dst_meshlet.vertex_count         = src_meshlet.end_vertex_indices_index    - src_meshlet.begin_vertex_indices_index;
		
		dst_meshlet.current_level_error_metric.center = float3(src_meshlet.current_level_error_metric.bounds.center);
		dst_meshlet.coarser_level_error_metric.center = float3(src_meshlet.coarser_level_error_metric.bounds.center);
		dst_meshlet.current_level_error_metric.radius = src_meshlet.current_level_error_metric.bounds.radius;
		dst_meshlet.coarser_level_error_metric.radius = src_meshlet.coarser_level_error_metric.bounds.radius;
		dst_meshlet.current_level_error_metric.error  = src_meshlet.current_level_error_metric.error;
		dst_meshlet.coarser_level_error_metric.error  = src_meshlet.coarser_level_error_metric.error;
	}
	
	MdtFreeContinuousLodBuildResult(&result, &callbacks);
	
	
	auto runtime_filepath = StringFormat(alloc, "./Assets/Runtime/%x..mrd"_sl, runtime_data_guid);
	
	auto runtime_file = SystemOpenFile(alloc, runtime_filepath, OpenFileFlags::Write);
	DebugAssert(runtime_file.handle, "Failed to open mesh output file.");
	defer{ SystemCloseFile(runtime_file); };
	
	MeshRuntimeDataLayout runtime_data_layout;
	runtime_data_layout.file_guid     = runtime_data_guid;
	runtime_data_layout.vertex_count  = (u32)result_vertices.count;
	runtime_data_layout.meshlet_count = (u32)result_meshlets.count;
	runtime_data_layout.indices_count = (u32)result_indices.count;
	
	SystemWriteFile(runtime_file, result_vertices.data, result_vertices.count * sizeof(BasicVertex),  runtime_data_layout.VertexBufferOffset());
	SystemWriteFile(runtime_file, result_meshlets.data, result_meshlets.count * sizeof(BasicMeshlet), runtime_data_layout.MeshletBufferOffset());
	SystemWriteFile(runtime_file, result_indices.data,  result_indices.count  * sizeof(u8),           runtime_data_layout.IndexBufferOffset());
	
	return runtime_data_layout;
}
