#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

static void BuildParametricSphere(StackAllocator* alloc, Array<float4>& result_vertices, Array<uint3>& result_triangles) {
	ProfilerScope("BuildParametricSphere");
	
	float3 veritces[6] = { // Octahedron vertices.
		float3(+1.f, 0.f, 0.f),
		float3(0.f, +1.f, 0.f),
		float3(0.f, 0.f, +1.f),
		float3(-1.f, 0.f, 0.f),
		float3(0.f, -1.f, 0.f),
		float3(0.f, 0.f, -1.f),
	};
	
	uint3 indices[8] = {
		uint3(0, 1, 2), // Upper hemisphere.
		uint3(1, 3, 2),
		uint3(3, 4, 2),
		uint3(4, 0, 2),
		
		uint3(0, 5, 1), // Lower hemisphere.
		uint3(1, 5, 3),
		uint3(3, 5, 4),
		uint3(4, 5, 0),
	};
	
	compile_const u32 grid_segment_count  = 4;
	compile_const u32 grid_vertex_count   = (grid_segment_count + 1) * (grid_segment_count + 2) / 2;
	compile_const u32 grid_triangle_count = (grid_segment_count * grid_segment_count);
	compile_const u32 mesh_vertex_count   = grid_vertex_count   * 8;
	compile_const u32 mesh_triangle_count = grid_triangle_count * 8;
	
	ArrayReserve(result_vertices,  alloc, mesh_vertex_count);
	ArrayReserve(result_triangles, alloc, mesh_triangle_count);
	
	for (u32 i = 0; i < 8; i += 1) {
		auto triangle = indices[i];
		float3 p0 = veritces[triangle.x];
		float3 p1 = veritces[triangle.y];
		float3 p2 = veritces[triangle.z];
		
		for (u32 u = 0; u <= grid_segment_count; u += 1) {
			for (u32 v = 0; v <= grid_segment_count - u; v += 1) {
				float uf = u * (1.f / (float)grid_segment_count);
				float vf = v * (1.f / (float)grid_segment_count);
				float wf = 1.f - uf - vf;
				
				auto position = Math::Normalize(p0 * uf + p1 * vf + p2 * wf);
				ArrayAppend(result_vertices, float4(position, 0.f));
			}
		}
		
		u32 base_index = i * grid_vertex_count;
		u32 next_row_base_index = base_index + grid_segment_count + 1;
		for (u32 u = 0; u < grid_segment_count; u += 1) {
			u32 i0 = base_index;
			u32 i1 = next_row_base_index;
			
			for (u32 v = 0; v < grid_segment_count - 1 - u; v += 1) {
				ArrayAppend(result_triangles, uint3(i0 + v, i1 + v, i0 + v + 1));
				ArrayAppend(result_triangles, uint3(i0 + v + 1, i1 + v, i1 + v + 1));
			}
			
			u32 v = grid_segment_count - u - 1;
			ArrayAppend(result_triangles, uint3(i0 + v, i1 + v, i0 + v + 1));
			
			base_index = next_row_base_index;
			next_row_base_index += grid_segment_count - u;
		}
	}
}

static void BuildParametricCube(StackAllocator* alloc, Array<float4>& result_vertices, Array<uint3>& result_triangles) {
	ProfilerScope("BuildParametricCube");
	
	ArrayReserve(result_vertices,  alloc, 8);
	ArrayReserve(result_triangles, alloc, 12);
	
	ArrayAppend(result_vertices, float4(-1.f, -1.f, -1.f, 0.f));
	ArrayAppend(result_vertices, float4(+1.f, -1.f, -1.f, 0.f));
	ArrayAppend(result_vertices, float4(-1.f, +1.f, -1.f, 0.f));
	ArrayAppend(result_vertices, float4(+1.f, +1.f, -1.f, 0.f));
	ArrayAppend(result_vertices, float4(-1.f, -1.f, +1.f, 0.f));
	ArrayAppend(result_vertices, float4(+1.f, -1.f, +1.f, 0.f));
	ArrayAppend(result_vertices, float4(-1.f, +1.f, +1.f, 0.f));
	ArrayAppend(result_vertices, float4(+1.f, +1.f, +1.f, 0.f));
	
	ArrayAppend(result_triangles, uint3(0, 1, 5));
	ArrayAppend(result_triangles, uint3(0, 2, 3));
	ArrayAppend(result_triangles, uint3(0, 3, 1));
	ArrayAppend(result_triangles, uint3(0, 4, 6));
	ArrayAppend(result_triangles, uint3(0, 5, 4));
	ArrayAppend(result_triangles, uint3(0, 6, 2));
	ArrayAppend(result_triangles, uint3(1, 3, 7));
	ArrayAppend(result_triangles, uint3(1, 7, 5));
	ArrayAppend(result_triangles, uint3(2, 6, 7));
	ArrayAppend(result_triangles, uint3(2, 7, 3));
	ArrayAppend(result_triangles, uint3(4, 5, 7));
	ArrayAppend(result_triangles, uint3(4, 7, 6));
}

static void BuildParametricCylinder(StackAllocator* alloc, Array<float4>& result_vertices, Array<uint3>& result_triangles) {
	ProfilerScope("BuildParametricCylinder");
	
	compile_const u32 segment_count       = 16;
	compile_const u32 mesh_vertex_count   = segment_count * 2 + 2;
	compile_const u32 mesh_triangle_count = segment_count * 4;
	
	ArrayReserve(result_vertices,  alloc, mesh_vertex_count);
	ArrayReserve(result_triangles, alloc, mesh_triangle_count);
	
	ArrayAppend(result_vertices, float4(0.f, 0.f, 1.f, 0.f));
	ArrayAppend(result_vertices, float4(0.f, 0.f, 0.f, 0.f));
	
	for (u32 i = 0; i < segment_count; i += 1) {
		float angle = (float)i * (1.f / (float)segment_count) * Math::TAU;
		
		auto position = float2(cosf(angle), sinf(angle));
		ArrayAppend(result_vertices, float4(position, 1.f, 0.f));
		ArrayAppend(result_vertices, float4(position, 0.f, 0.f));
	}
	
	for (u32 i = 0; i < segment_count; i += 1) {
		u32 i0 = i * 2 + 2;
		u32 i1 = ((i + 1) % segment_count) * 2 + 2;
		
		ArrayAppend(result_triangles, uint3(0, i0, i1));
		ArrayAppend(result_triangles, uint3(i0, i1 + 1, i1));
		ArrayAppend(result_triangles, uint3(i0, i0 + 1, i1 + 1));
		ArrayAppend(result_triangles, uint3(i0 + 1, 1, i1 + 1));
	}
}

static void BuildParametricTorus(StackAllocator* alloc, Array<float4>& result_vertices, Array<uint3>& result_triangles) {
	ProfilerScope("BuildParametricTorus");
	
	compile_const u32 major_segment_count = 32;
	compile_const u32 minor_segment_count = 16;
	compile_const u32 mesh_vertex_count   = major_segment_count * minor_segment_count;
	compile_const u32 mesh_triangle_count = major_segment_count * minor_segment_count * 2;
	
	ArrayReserve(result_vertices,  alloc, mesh_vertex_count);
	ArrayReserve(result_triangles, alloc, mesh_triangle_count);
	
	for (u32 i = 0; i < major_segment_count; i += 1) {
		float major_angle = (float)i * (1.f / (float)major_segment_count) * Math::TAU;
		auto major_position = float2(cosf(major_angle), sinf(major_angle));
		
		for (u32 j = 0; j < minor_segment_count; j += 1) {
			float minor_angle = (float)j * (1.f / (float)minor_segment_count) * Math::TAU;
			auto minor_position = float2(cosf(minor_angle), sinf(minor_angle));
			
			ArrayAppend(result_vertices, float4(major_position, minor_position));
		}
	}
	
	for (u32 i = 0; i < major_segment_count; i += 1) {
		u32 i0 = i * minor_segment_count;
		u32 i1 = ((i + 1) % major_segment_count) * minor_segment_count;
		
		for (u32 j0 = 0; j0 < minor_segment_count; j0 += 1) {
			u32 j1 = (j0 + 1) % minor_segment_count;
			
			ArrayAppend(result_triangles, uint3(i0 + j0, i1 + j1, i1 + j0));
			ArrayAppend(result_triangles, uint3(i0 + j0, i0 + j1, i1 + j1));
		}
	}
}

void DebugGeometryRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget render_target;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer rasterizer;
	} pipeline;
	
	pipeline.render_target.format = TextureFormat::R16G16B16A16_FLOAT;
	pipeline.depth_stencil.flags  = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format = TextureFormat::D32_FLOAT;
	pipeline.rasterizer.cull_mode = PipelineRasterizer::CullMode::Back;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DebugGeometryShadersID);
}

void DebugGeometryRenderPass::RecordPass(RecordContext* record_context) {
	if (debug_mesh_instance_arrays.count == 0) return;
	
	CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
	CmdSetRenderTargets(record_context, VirtualResourceID::SceneRadiance, VirtualResourceID::DepthStencil);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.scene, scene_constants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	for (auto [instance_type, debug_mesh_instances] : debug_mesh_instance_arrays) {
		DebugAssert(debug_mesh_instances.count != 0, "Empty debug_mesh_instances.");
		
		// TODO: Use 16 bit types.
		Array<float4> result_vertices;
		Array<uint3> result_triangles;
		
		// TODO: Build meshes on startup and upload them to VRAM.
		switch (instance_type) {
		case DebugMeshInstanceType::Sphere:   BuildParametricSphere(record_context->alloc,   result_vertices, result_triangles); break;
		case DebugMeshInstanceType::Cube:     BuildParametricCube(record_context->alloc,     result_vertices, result_triangles); break;
		case DebugMeshInstanceType::Cylinder: BuildParametricCylinder(record_context->alloc, result_vertices, result_triangles); break;
		case DebugMeshInstanceType::Torus:    BuildParametricTorus(record_context->alloc,    result_vertices, result_triangles); break;
		default: continue;
		}
		
		auto [vertex_buffer_gpu_address, vertex_buffer] = AllocateTransientUploadBuffer<float4>(record_context, (u32)result_vertices.count);
		auto [index_buffer_gpu_address,  index_buffer]  = AllocateTransientUploadBuffer<uint3>(record_context, (u32)result_triangles.count);
		
		memcpy(vertex_buffer, result_vertices.data, result_vertices.count * sizeof(float4));
		memcpy(index_buffer, result_triangles.data, result_triangles.count * sizeof(uint3));
		
		// TODO: Merge all instance lists.
		auto [instances_gpu_address, instances] = AllocateTransientUploadBuffer<DebugMeshInstance>(record_context, (u32)debug_mesh_instances.count);
		memcpy(instances, debug_mesh_instances.data, debug_mesh_instances.count * sizeof(DebugMeshInstance));
		
		auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
		descriptor_table.vertices.Bind(vertex_buffer_gpu_address, (u32)(result_vertices.count * sizeof(float4)));
		descriptor_table.instances.Bind(instances_gpu_address, (u32)(debug_mesh_instances.count * sizeof(DebugMeshInstance)));
		
		DebugGeometryPushConstants constants;
		constants.instance_type = instance_type;
		
		CmdSetRootArgument(record_context, root_signature.constants, constants);
		CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
		CmdSetIndexBufferView(record_context, index_buffer_gpu_address, (u32)(result_triangles.count * sizeof(uint3)), TextureFormat::R32_UINT);
		
		CmdDrawIndexedInstanced(record_context, (u32)(result_triangles.count * 3), (u32)debug_mesh_instances.count);
	}
}

