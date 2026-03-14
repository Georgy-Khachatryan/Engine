#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "GraphicsApi/AsyncTransferQueue.h"

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

DebugGeometryBuffer DebugGeometryRenderPass::CreateDebugGeometryBuffer(StackAllocator* alloc, GraphicsContext* graphics_context, AsyncTransferQueue* async_transfer_queue) {
	DebugGeometryBuffer result;
	
	struct DebugMeshBuffers {
		// TODO: Use 16 bit types.
		Array<float4> vertices;
		Array<uint3> triangles;
	};
	
	FixedCountArray<DebugMeshBuffers, (u32)DebugMeshInstanceType::Count> mesh_type_buffers;
	
	for (u32 i = 0; i < (u32)DebugMeshInstanceType::Count; i += 1) {
		auto& buffers = mesh_type_buffers[i];
		
		switch ((DebugMeshInstanceType)i) {
		case DebugMeshInstanceType::Sphere:   BuildParametricSphere(alloc,   buffers.vertices, buffers.triangles); break;
		case DebugMeshInstanceType::Cube:     BuildParametricCube(alloc,     buffers.vertices, buffers.triangles); break;
		case DebugMeshInstanceType::Cylinder: BuildParametricCylinder(alloc, buffers.vertices, buffers.triangles); break;
		case DebugMeshInstanceType::Torus:    BuildParametricTorus(alloc,    buffers.vertices, buffers.triangles); break;
		}
		
		auto& layout = result.mesh_layouts[i];
		layout.vertex_offset = result.vertex_count;
		layout.index_offset  = result.index_count;
		layout.vertex_count  = (u32)(buffers.vertices.count);
		layout.index_count   = (u32)(buffers.triangles.count * 3);
		
		result.vertex_count += layout.vertex_count;
		result.index_count  += layout.index_count;
	}
	
	u64 mesh_buffer_size = result.vertex_count * sizeof(float4) + result.index_count * sizeof(u32);
	auto mesh_buffer = CreateBufferResource(graphics_context, (u32)mesh_buffer_size, CreateResourceFlags::None);
	
	u64 vertex_buffer_offset = 0;
	u64 index_buffer_offset  = result.vertex_count * sizeof(float4);
	for (auto& buffers : mesh_type_buffers) {
		u64 vertex_buffer_size = buffers.vertices.count * sizeof(float4);
		u64 index_buffer_size  = buffers.triangles.count * sizeof(uint3);
		
		AsyncCopyMemoryToBuffer(async_transfer_queue, mesh_buffer, vertex_buffer_offset, mesh_buffer_size, buffers.vertices.data, vertex_buffer_size);
		AsyncCopyMemoryToBuffer(async_transfer_queue, mesh_buffer, index_buffer_offset, mesh_buffer_size, buffers.triangles.data, index_buffer_size);
		
		vertex_buffer_offset += vertex_buffer_size;
		index_buffer_offset  += index_buffer_size;
	}
	
	result.resource      = mesh_buffer;
	result.resource_size = mesh_buffer_size;
	
	return result;
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
	
	CmdClearDepthStencil(record_context, VirtualResourceID::DebugGeometryDepthStencil);
	CmdSetRenderTargets(record_context, VirtualResourceID::SceneRadiance, VirtualResourceID::DebugGeometryDepthStencil);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	u64 debug_mesh_instance_count = 0;
	for (auto [instance_type, debug_mesh_instances] : debug_mesh_instance_arrays) {
		DebugAssert(debug_mesh_instances.count != 0, "Empty debug mesh instance array.");
		debug_mesh_instance_count += debug_mesh_instances.count;
	}
	
	auto [instances_gpu_address, instances] = AllocateTransientUploadBuffer<DebugMeshInstance>(record_context, (u32)debug_mesh_instance_count);
	for (auto [instance_type, debug_mesh_instances] : debug_mesh_instance_arrays) {
		memcpy(instances, debug_mesh_instances.data, debug_mesh_instances.count * sizeof(DebugMeshInstance));
		instances += debug_mesh_instances.count;
	}
	
	auto vertex_buffer_gpu_address = GpuAddress(VirtualResourceID::DebugMeshBuffer, 0u);
	auto index_buffer_gpu_address  = GpuAddress(VirtualResourceID::DebugMeshBuffer, (u32)(debug_geometry_buffer->vertex_count * sizeof(float4)));
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.vertices.Bind(vertex_buffer_gpu_address, (u32)(debug_geometry_buffer->vertex_count * sizeof(float4)));
	descriptor_table.instances.Bind(instances_gpu_address, (u32)(debug_mesh_instance_count * sizeof(DebugMeshInstance)));
	
	CmdSetIndexBufferView(record_context, index_buffer_gpu_address, (u32)(debug_geometry_buffer->index_count * sizeof(u32)), TextureFormat::R32_UINT);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	u32 mesh_instance_offset = 0;
	for (auto [instance_type, debug_mesh_instances] : debug_mesh_instance_arrays) {
		auto& layout = debug_geometry_buffer->mesh_layouts[(u32)instance_type];
		
		CmdSetRootArgument(record_context, root_signature.constants, { instance_type });
		CmdDrawIndexedInstanced(record_context, layout.index_count, (u32)debug_mesh_instances.count, layout.index_offset, layout.vertex_offset, mesh_instance_offset);
		
		mesh_instance_offset += (u32)debug_mesh_instances.count;
	}
}

