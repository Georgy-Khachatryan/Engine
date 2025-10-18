#include "GraphicsApi.h"
#include "RecordContext.h"

PipelineID CreateComputePipeline(PipelineLibrary* lib, ShaderID shader_id, u64 permutation) {
	u32 pipeline_index = (u32)lib->pipeline_definitions.count;
	
	auto& pipeline_definition = ArrayEmplace(lib->pipeline_definitions, lib->alloc);
	pipeline_definition.shader_id         = shader_id;
	pipeline_definition.permutation       = permutation;
	pipeline_definition.shader_type_mask  = ShaderTypeMask::ComputeShader;
	pipeline_definition.root_signature_id = lib->current_pass_root_signature_id;
	
	return PipelineID{ pipeline_index };
}

PipelineID CreateGraphicsPipeline(PipelineLibrary* lib, ArrayView<u8> pipeline_state_stream, ShaderID shader_id, u64 permutation, ShaderTypeMask shader_type_mask) {
	u32 pipeline_index = (u32)lib->pipeline_definitions.count;
	
	auto& pipeline_definition = ArrayEmplace(lib->pipeline_definitions, lib->alloc);
	pipeline_definition.shader_id             = shader_id;
	pipeline_definition.permutation           = permutation;
	pipeline_definition.shader_type_mask      = shader_type_mask;
	pipeline_definition.root_signature_id     = lib->current_pass_root_signature_id;
	pipeline_definition.pipeline_state_stream = ArrayCopy(pipeline_state_stream, lib->alloc);
	
	return PipelineID{ pipeline_index };
}

static PipelineDepthStencil default_depth_stencil = {};
static PipelineRasterizer   default_rasterizer    = {};

PipelineStateDescription CreatePipelineStateDescription(ArrayView<u8> stream) {
	PipelineStateDescription result;
	result.depth_stencil = &default_depth_stencil;
	result.rasterizer    = &default_rasterizer;
	
	u64 cursor = 0;
	while (cursor < stream.count) {
		switch ((PipelineStateType)stream[cursor]) {
		case PipelineStateType::BlendState: {
			ArrayAppend(result.blend_states, (const PipelineBlendState*)(stream.data + cursor));
			cursor += sizeof(PipelineBlendState);
			break;
		} case PipelineStateType::RenderTarget: {
			ArrayAppend(result.render_targets, (const PipelineRenderTarget*)(stream.data + cursor));
			cursor += sizeof(PipelineRenderTarget);
			break;
		} case PipelineStateType::DepthStencil: {
			result.depth_stencil = (const PipelineDepthStencil*)(stream.data + cursor);
			cursor += sizeof(PipelineDepthStencil);
			break;
		} case PipelineStateType::Rasterizer: {
			result.rasterizer = (const PipelineRasterizer*)(stream.data + cursor);
			cursor += sizeof(PipelineRasterizer);
			break;
		} default: {
			DebugAssertAlways("Unhandled PipelineStateType '%u'.", (u32)stream[cursor]);
			cursor = stream.count;
			break;
		}
		}
	}
	
	DebugAssert(result.blend_states.count == 1 && result.render_targets.count != 0 || result.blend_states.count == result.render_targets.count, "Mismatching render target and blend state counts. Render targets: %llu, Blend States: %llu.", result.render_targets.count, result.blend_states.count);
	
	return result;
}

static_assert(alignof(PipelineBlendState) == sizeof(u8), "Incorrect PipelineBlendState alignment.");
static_assert(alignof(PipelineRenderTarget) == sizeof(u8), "Incorrect PipelineRenderTarget alignment.");
static_assert(alignof(PipelineDepthStencil) == sizeof(u8), "Incorrect PipelineDepthStencil alignment.");
static_assert(alignof(PipelineRasterizer) == sizeof(u8), "Incorrect PipelineRasterizer alignment.");


TextureSize GetTextureSize(RecordContext* record_context, VirtualResourceID resource_id) {
	auto& resource = record_context->resource_table->virtual_resources[(u32)resource_id];
	DebugAssert(resource.type == VirtualResource::Type::VirtualTexture || resource.type == VirtualResource::Type::NativeTexture, "Resource is not a texture.");
	return resource.texture.size;
}

VirtualResource& GetVirtualResource(RecordContext* record_context, VirtualResourceID resource_id) {
	return record_context->resource_table->virtual_resources[(u32)resource_id];
}
