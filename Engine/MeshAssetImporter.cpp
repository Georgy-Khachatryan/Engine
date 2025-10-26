#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicMath.h"
#include "RenderPasses.h"

#include <SDK/ufbx/ufbx.h>

static float3x4 LoadUfbxMatrix(const ufbx_matrix& m) {
	float3x4 result;
	result[0] = float4(m.m00, m.m01, m.m02, m.m03);
	result[1] = float4(m.m10, m.m11, m.m12, m.m13);
	result[2] = float4(m.m20, m.m21, m.m22, m.m23);
	return result;
}

void ImportFbxMeshFile(StackAllocator* alloc, String filepath, Array<BasicVertex>& result_vertices, Array<u32>& result_indices) {
	auto file_data = SystemReadFileToString(alloc, filepath);
	DebugAssert(file_data.data != nullptr, "Failed to load mesh file '%.*s'", (s32)filepath.count, filepath.data);
	
	ufbx_load_opts options = {};
	options.ignore_animation = true;
	options.ignore_embedded  = true;
	options.target_axes      = ufbx_axes_right_handed_z_up;
	options.target_unit_meters = 1.f;
	
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
	
	ArrayReserve(result_vertices, alloc, max_result_triangles * 3);
	ArrayReserve(result_indices,  alloc, max_result_triangles * 3);
	
	
	TempAllocationScope(alloc);
	
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
			
			u32 index_offset = (u32)result_vertices.count;
			for (auto& vertex : mesh_vertices) {
				auto instance_vertex = vertex;
				instance_vertex.position = geometry_to_world * float4(instance_vertex.position, 1.f);
				ArrayAppend(result_vertices, instance_vertex);
			}
			
			for (u32 index : mesh_indices) {
				ArrayAppend(result_indices, index + index_offset);
			}
		}
	}
}
