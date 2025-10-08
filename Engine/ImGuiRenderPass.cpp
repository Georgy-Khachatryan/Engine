#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApiD3D12.h"
#include "GraphicsApi/RecordContext.h"
#include "Basic/BasicFiles.h"

#include <SDK/imgui/imgui.h>

void ImGuiRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateGraphicsPipeline(lib, ImGuiShadersID);
}

void ImGuiRenderPass::RecordPass(RecordContext* record_context) {
	auto upload_buffer = record_context->resource_table->virtual_resources[(u32)VirtualResourceID::ImGuiUploadBuffer].buffer;
	
	ImGui::Render();
	
	auto* draw_data = ImGui::GetDrawData();
	
	u32 upload_buffer_offset = 0;
	if (draw_data->Textures != nullptr) {
		for (auto* texture : *draw_data->Textures) {
			if (texture->Status == ImTextureStatus_WantCreate) {
				auto size = TextureSize(TextureFormat::R8G8B8A8_UNORM, texture->Width, texture->Height);
				auto resource = CreateTextureResource(record_context->context, size);
				texture->SetTexID(1024); // TODO: Allocate texture IDs dynamically.
				texture->BackendUserData = resource.d3d12;
				record_context->resource_table->Set(VirtualResourceID::ImGuiFontTexture, resource, size);
			}
			
			if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
				u32 upload_x = (texture->Status == ImTextureStatus_WantCreate) ? 0 : texture->UpdateRect.x;
				u32 upload_y = (texture->Status == ImTextureStatus_WantCreate) ? 0 : texture->UpdateRect.y;
				u32 upload_w = (texture->Status == ImTextureStatus_WantCreate) ? texture->Width  : texture->UpdateRect.w;
				u32 upload_h = (texture->Status == ImTextureStatus_WantCreate) ? texture->Height : texture->UpdateRect.h;
				
				u32 upload_pitch_src = upload_w * texture->BytesPerPixel;
				u32 upload_pitch_dst = (u32)AlignUp(upload_pitch_src, 256u);
				u32 upload_size      = upload_pitch_dst * upload_h;
				DebugAssert(upload_size < upload_buffer.size, "ImGui upload buffer overflow. %u/%u", upload_size, upload_buffer.size);
				
				u8* mapped = upload_buffer.cpu_address + upload_buffer_offset;
				
				for (u32 y = 0; y < upload_h; y += 1) {
					memcpy(mapped + y * upload_pitch_dst, texture->GetPixelsAt(upload_x, upload_y + y), upload_pitch_src);
				}
				
				CmdCopyBufferToTexture(
					record_context,
					VirtualResourceID::ImGuiUploadBuffer,
					VirtualResourceID::ImGuiFontTexture,
					upload_buffer_offset,
					upload_pitch_dst,
					uint3(upload_w, upload_h, 1),
					0,
					uint3(upload_x, upload_y, 0)
				);
				
				upload_buffer_offset += upload_size;
				
				texture->SetStatus(ImTextureStatus_OK);
			}
			
			if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames >= number_of_frames_in_flight) {
				// TODO: Deallocate textures.
				texture->SetTexID(ImTextureID_Invalid);
				texture->SetStatus(ImTextureStatus_Destroyed);
			}
		}
	}
	
	upload_buffer_offset = (u32)RoundUp(upload_buffer_offset, sizeof(ImDrawVert));
	
	u32 total_size = draw_data->TotalVtxCount * sizeof(ImDrawVert) + draw_data->TotalIdxCount * sizeof(ImDrawIdx) ;
	DebugAssert(total_size + upload_buffer_offset <= upload_buffer.size, "ImGui upload buffer overflow. %u/%u", total_size+ upload_buffer_offset, upload_buffer.size);
	
	if (total_size == 0) return;
	
	auto* vtx_dst = (ImDrawVert*)(upload_buffer.cpu_address + upload_buffer_offset);
	auto* idx_dst = (ImDrawIdx*)(vtx_dst + draw_data->TotalVtxCount);
	for (auto* draw_list : draw_data->CmdLists) {
		memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vtx_dst += draw_list->VtxBuffer.Size;
		idx_dst += draw_list->IdxBuffer.Size;
	}
	
	
	FixedCountArray<VirtualResourceID, 1> render_targets;
	render_targets[0] = VirtualResourceID::CurrentBackBuffer;
	CmdClearRenderTarget(record_context, render_targets[0]);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.vertices.Bind(VirtualResourceID::ImGuiUploadBuffer, upload_buffer_offset, draw_data->TotalVtxCount * sizeof(ImDrawVert));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	CmdSetRenderTargets(record_context, render_targets);
	
	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
	
	ImGuiPushConstants constants;
	constants.view_to_clip_coef = float4(2.f / (R - L), 2.f / (T - B), (R + L) / (L - R), (T + B) / (B - T));
	CmdSetRootArgument(record_context, root_signature.constants, constants);
	
	CmdSetIndexBufferView(record_context, VirtualResourceID::ImGuiUploadBuffer, draw_data->TotalVtxCount * sizeof(ImDrawVert) + upload_buffer_offset, draw_data->TotalIdxCount * sizeof(ImDrawIdx), TextureFormat::R16_UINT);
	
	auto clip_offset = float2(draw_data->DisplayPos);
	auto clip_scale  = float2(draw_data->FramebufferScale);
	
	CmdSetViewport(record_context, uint2(float2(draw_data->DisplaySize) * clip_scale));
	
	// (Because we merged all buffers into a single one, we maintain our own offset into them).
	u32 global_vtx_offset = 0;
	u32 global_idx_offset = 0;
	for (auto* draw_list : draw_data->CmdLists) {
		for (s32 cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i += 1) {
			const auto* pcmd = &draw_list->CmdBuffer[cmd_i];
			
			// Project scissor/clipping rectangles into framebuffer space.
			auto clip_rect = float4(pcmd->ClipRect);
			auto clip_min = (clip_rect.xy - clip_offset) * clip_scale;
			auto clip_max = (clip_rect.zw - clip_offset) * clip_scale;
			if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;
			
			CmdSetScissor(record_context, uint2(clip_max), uint2(clip_min));
			
			// TODO: Bind textures.
			CmdDrawIndexedInstanced(record_context, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
		}
		
		global_idx_offset += draw_list->IdxBuffer.Size;
		global_vtx_offset += draw_list->VtxBuffer.Size;
	}
}

