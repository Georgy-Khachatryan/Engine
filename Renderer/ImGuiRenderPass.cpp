#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

#include <SDK/imgui/imgui.h>


static void ImGuiUpdateTextures(RecordContext* record_context, ImVector<ImTextureData*>& textures) {
	for (auto* texture : textures) {
		auto size = TextureSize(TextureFormat::R8G8B8A8_UNORM_SRGB, texture->Width, texture->Height);
		
		if (texture->Status == ImTextureStatus_WantCreate) {
			texture->SetTexID(AllocatePersistentSrvDescriptor(record_context->context));
			texture->BackendUserData = CreateTextureResource(record_context->context, size).handle;
		}
		
		if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
			auto texture_id = record_context->resource_table->AddTransient({ texture->BackendUserData }, size);
			
			if (texture->Status == ImTextureStatus_WantCreate) {
				CreateResourceDescriptor(record_context, HLSL::Texture2D<float4>(texture_id), (u32)texture->GetTexID());
			}
			
			auto upload_offset = (texture->Status == ImTextureStatus_WantCreate) ? uint2(0, 0) : uint2(texture->UpdateRect.x, texture->UpdateRect.y);
			auto upload_size   = (texture->Status == ImTextureStatus_WantCreate) ? uint2(size) : uint2(texture->UpdateRect.w, texture->UpdateRect.h);
			
			u32 upload_pitch_src = upload_size.x * texture->BytesPerPixel;
			u32 upload_pitch_dst = AlignUp(upload_pitch_src, texture_row_pitch_alignment);
			
			auto [gpu_address, cpu_address] = AllocateTransientUploadBuffer(record_context, upload_pitch_dst * upload_size.y);
			
			for (u32 y = 0; y < upload_size.y; y += 1) {
				memcpy(cpu_address + y * upload_pitch_dst, texture->GetPixelsAt(upload_offset.x, upload_offset.y + y), upload_pitch_src);
			}
			CmdCopyBufferToTexture(record_context, gpu_address, texture_id, upload_pitch_dst, uint3(upload_size, 1), uint3(upload_offset, 0));
			
			texture->SetStatus(ImTextureStatus_OK);
		}
		
		if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames >= number_of_frames_in_flight) {
			DeallocatePersistentSrvDescriptor(record_context->context, (u32)texture->GetTexID());
			ReleaseTextureResource(record_context->context, { texture->BackendUserData });
			
			texture->SetTexID(ImTextureID_Invalid);
			texture->SetStatus(ImTextureStatus_Destroyed);
			texture->BackendUserData = nullptr;
		}
	}
}

void ImGuiRenderPass::CreatePipelines(PipelineLibrary* lib) {
	struct {
		PipelineBlendState blend_state;
		PipelineRenderTarget render_target;
	} pipeline;
	
	pipeline.blend_state.src_blend_rgb = PipelineBlendState::Blend::SrcAlpha;
	pipeline.blend_state.dst_blend_rgb = PipelineBlendState::Blend::InvSrcAlpha;
	pipeline.blend_state.blend_op_rgb  = PipelineBlendState::BlendOp::Add;
	pipeline.blend_state.src_blend_a   = PipelineBlendState::Blend::One;
	pipeline.blend_state.dst_blend_a   = PipelineBlendState::Blend::InvSrcAlpha;
	pipeline.blend_state.blend_op_a    = PipelineBlendState::BlendOp::Add;
	
	pipeline.render_target.format = TextureFormat::R8G8B8A8_UNORM_SRGB;
	sdr_pipeline_id = CreateGraphicsPipeline(lib, pipeline, ImGuiShadersID);
	
	pipeline.render_target.format = TextureFormat::R16G16B16A16_FLOAT;
	hdr_pipeline_id = CreateGraphicsPipeline(lib, pipeline, ImGuiShadersID);
}

void ImGuiRenderPass::RecordPass(RecordContext* record_context) {
	ImGui::Render();
	auto* draw_data = ImGui::GetDrawData();
	
	ImGuiUpdateTextures(record_context, *draw_data->Textures);
	
	if (draw_data->CmdListsCount == 0 || draw_data->DisplaySize.x <= 0.f || draw_data->DisplaySize.y <= 0.f) return;
	
	
	compile_const u32 vertex_size = sizeof(ImDrawVert); // Buffers must be aligned to structure size.
	auto [vertex_buffer_gpu_address, vertex_buffer] = AllocateTransientUploadBuffer<ImDrawVert, vertex_size>(record_context, draw_data->TotalVtxCount);
	auto [index_buffer_gpu_address,  index_buffer]  = AllocateTransientUploadBuffer<ImDrawIdx>(record_context, draw_data->TotalIdxCount);
	
	for (auto* draw_list : draw_data->CmdLists) {
		memcpy(vertex_buffer, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(index_buffer,  draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vertex_buffer += draw_list->VtxBuffer.Size;
		index_buffer  += draw_list->IdxBuffer.Size;
	}
	
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::CurrentBackBuffer);
	CmdClearRenderTarget(record_context, VirtualResourceID::CurrentBackBuffer);
	CmdSetRenderTargets(record_context,  VirtualResourceID::CurrentBackBuffer);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.vertices.Bind(vertex_buffer_gpu_address, draw_data->TotalVtxCount * sizeof(ImDrawVert));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetPipelineState(record_context, render_target_size.format == TextureFormat::R8G8B8A8_UNORM_SRGB ? sdr_pipeline_id : hdr_pipeline_id);
	
	
	auto display_min = float2(draw_data->DisplayPos.x, draw_data->DisplayPos.y + draw_data->DisplaySize.y);
	auto display_max = float2(draw_data->DisplayPos.x + draw_data->DisplaySize.x, draw_data->DisplayPos.y);
	
	ImGuiPushConstants constants;
	constants.view_to_clip_coef.xy = float2(2.f, -2.f) / float2(draw_data->DisplaySize);
	constants.view_to_clip_coef.zw = (display_min + display_max) / (display_min - display_max);
	
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetIndexBufferView(record_context, index_buffer_gpu_address, draw_data->TotalIdxCount * sizeof(ImDrawIdx));
	
	auto clip_offset = float2(draw_data->DisplayPos);
	auto clip_scale  = float2(draw_data->FramebufferScale);
	
	CmdSetViewport(record_context, uint2(float2(draw_data->DisplaySize) * clip_scale));
	
	ImGuiTextureIdPushConstants texture_id;
	texture_id.index = u32_max;
	
	u32 vertex_offset = 0;
	u32 index_offset  = 0;
	for (auto* draw_list : draw_data->CmdLists) {
		for (auto& command : draw_list->CmdBuffer) {
			auto clip_rect = float4(command.ClipRect);
			
			auto clip_min = (clip_rect.xy - clip_offset) * clip_scale;
			auto clip_max = (clip_rect.zw - clip_offset) * clip_scale;
			if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;
			
			CmdSetScissor(record_context, uint2(clip_max), uint2(clip_min));
			
			if (texture_id.index != (u32)command.GetTexID()) {
				texture_id.index  = (u32)command.GetTexID();
				CmdSetRootArgument(record_context, root_signature.texture_id, texture_id);
			}
			
			CmdDrawIndexedInstanced(record_context, command.ElemCount, 1, command.IdxOffset + index_offset, command.VtxOffset + vertex_offset);
		}
		
		vertex_offset += draw_list->VtxBuffer.Size;
		index_offset  += draw_list->IdxBuffer.Size;
	}
}

