#include "Basic/BasicBitArray.h"
#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"
#include "EntitySystem/EntitySystem.h"

void MeshletClearBuffersRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::ClearBuffers);
}

void MeshletClearBuffersRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	auto& meshlet_streaming_feedback = GetVirtualResource(record_context, VirtualResourceID::MeshletStreamingFeedback);
	CmdDispatch(record_context, DivideAndRoundUp(meshlet_streaming_feedback.buffer.size, (u32)(256u * sizeof(u32))));
}

void MeshletAllocateStreamingFeedbackRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::AllocateStreamingFeedback);
}

void MeshletAllocateStreamingFeedbackRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	auto* mesh_assets = QueryEntities<MeshAssetType>(record_context->alloc, *asset_system)[0];
	if (mesh_assets->capacity != 0) { // TODO: Use the minimize the dispatch size.
		CmdDispatch(record_context, DivideAndRoundUp(mesh_assets->capacity, 256u));
	}
}


void MeshletCullingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, MeshletCullingShadersID, MeshletCullingShaders::MeshletCulling);
}

void MeshletCullingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto* mesh_entities = QueryEntities<GpuMeshEntityQuery>(record_context->alloc, *world_system)[0];
	if (mesh_entities->capacity != 0) { // TODO: Use the minimize the dispatch size.
		CmdDispatch(record_context, 1u, mesh_entities->capacity);
	}
	
	
	auto& meshlet_streaming_feedback = GetVirtualResource(record_context, VirtualResourceID::MeshletStreamingFeedback);
	auto [readback_gpu_address, readback_cpu_address] = AllocateTransientReadbackBuffer(record_context, meshlet_streaming_feedback.buffer.size);
	
	CmdCopyBufferToBuffer(record_context, VirtualResourceID::MeshletStreamingFeedback, readback_gpu_address, meshlet_streaming_feedback.buffer.size);
	meshlet_streaming_feedback_queue->Store(readback_cpu_address, record_context->frame_index);
	
#if 0
	auto element = meshlet_streaming_feedback_queue->Load(record_context->frame_index);
	if (element.data) {
		u32* meshlet_streaming_feedback_data = (u32*)element.data;
		
		u32 read_index = 0;
		u32 size = meshlet_streaming_feedback_data[read_index++];
		while (read_index < size) {
			u32 mesh_asset_index     = meshlet_streaming_feedback_data[read_index++];
			u32 feedback_buffer_size = meshlet_streaming_feedback_data[read_index++];
			
			SystemWriteToConsole(record_context->alloc, "MeshAssetIndex: %\n"_sl, mesh_asset_index);
			for (u32 i = 0; i < feedback_buffer_size; i += 1) {
				u32 page_mask = meshlet_streaming_feedback_data[read_index++];
				
				for (u32 bit_index : BitScanLow32(page_mask)) {
					u32 page_index = i * 32u + bit_index;
					SystemWriteToConsole(record_context->alloc, "\tPageIndex: %\n"_sl, page_index);
				}
			}
		}
	}
#endif
}


void BasicMeshRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineRenderTarget scene_radiance;
		PipelineRenderTarget motion_vectors;
		PipelineDepthStencil depth_stencil;
		PipelineRasterizer rasterizer;
	} pipeline;
	
	pipeline.scene_radiance.format  = TextureFormat::R16G16B16A16_FLOAT;
	pipeline.motion_vectors.format = TextureFormat::R16G16_FLOAT;
	pipeline.depth_stencil.flags   = PipelineDepthStencil::Flags::EnableDepthWrite;
	pipeline.depth_stencil.format  = TextureFormat::D32_FLOAT;
	pipeline.rasterizer.cull_mode  = PipelineRasterizer::CullMode::Back;
	
	pipeline_id = CreateGraphicsPipeline(lib, pipeline, DrawTestMeshShadersID, 0, ShaderTypeMask::MeshShader | ShaderTypeMask::PixelShader);
}

void BasicMeshRenderPass::RecordPass(RecordContext* record_context) {
	CmdClearDepthStencil(record_context, VirtualResourceID::DepthStencil);
	CmdClearRenderTarget(record_context, VirtualResourceID::MotionVectors);
	
	FixedCountArray<VirtualResourceID, 2> render_targets;
	render_targets[0] = VirtualResourceID::SceneRadiance;
	render_targets[1] = VirtualResourceID::MotionVectors;
	CmdSetRenderTargets(record_context, render_targets, VirtualResourceID::DepthStencil);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.scene, VirtualResourceID::SceneConstants);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdSetViewportAndScissor(record_context, uint2(render_target_size));
	
	CmdDispatchMeshIndirect(record_context, VirtualResourceID::MeshletIndirectArguments);
}


void UpdateMeshletPageTableRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, UpdateMeshletPageTableShadersID);
}

void UpdateMeshletPageTableRenderPass::RecordPass(RecordContext* record_context) {
	auto entity_view = QueryEntities<MeshAssetType>(record_context->alloc, *asset_system);
	
	u32 page_list_size = 0;
	u32 command_count  = 0;
	for (auto* entity_array : entity_view) {
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->alive_mask)) {
			page_list_size += streams.runtime_data_layout[i].page_count;
			command_count  += 1;
		}
	}
	if (page_list_size == 0) return;
	
	
	auto [page_list_gpu_address, page_list_cpu_address] = AllocateTransientUploadBuffer<u32, 16u>(record_context, page_list_size);
	auto [commands_gpu_address,  commands_cpu_address]  = AllocateTransientUploadBuffer<MeshletPageTableUpdateCommand, 16u>(record_context, command_count);
	
	u32 page_list_offset = 0;
	u32 commands_offset  = 0;
	for (auto* entity_array : entity_view) {
		auto streams = ExtractComponentStreams<MeshAssetType>(entity_array);
		
		for (u64 i : BitArrayIt(entity_array->alive_mask)) {
			u32 page_count = streams.runtime_data_layout[i].page_count;
			
			MeshletPageTableUpdateCommand command;
			command.mesh_asset_index = (u32)i;
			command.page_list_offset = (u16)page_list_offset;
			command.page_list_count  = (u16)page_count;
			commands_cpu_address[commands_offset] = command;
			commands_offset += 1;
			
			u32 streamed_in_page_count = streams.allocation[i].streamed_in_page_count;
			for (u32 page_index = 0; page_index < page_count; page_index += 1) {
				auto command_type = page_index < streamed_in_page_count ? MeshletPageTableUpdateCommandType::PageIn : MeshletPageTableUpdateCommandType::PageOut;
				page_list_cpu_address[page_list_offset] = page_index | ((u32)command_type << 31);
				page_list_offset += 1;
			}
		}
	}
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.commands.Bind(commands_gpu_address, command_count * sizeof(MeshletPageTableUpdateCommand));
	descriptor_table.page_list.Bind(page_list_gpu_address, page_list_size * sizeof(u32));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, pipeline_id);
	
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	
	CmdDispatch(record_context, 1u, command_count);
}
