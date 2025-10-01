#include "GraphicsApiD3D12.h"
#include "Basic/BasicMemory.h"
#include "Engine/RenderPasses.h"
#include "RecordContext.h"
#include "RecordContextCommands.h"


static void FillDescriptorTables(GraphicsContextD3D12* context, ArrayView<HLSL::BaseDescriptorTable*> descriptor_tables) {
	auto cpu_base_handle = context->cpu_base_handles[(u32)DescriptorHeapType::SRV];
	auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
	auto* device = context->device;
	
	for (auto* descriptor_table : descriptor_tables) {
		auto descriptors = ArrayView<HLSL::ResourceDescriptor>{ (HLSL::ResourceDescriptor*)(descriptor_table + 1), (u64)descriptor_table->descriptor_count };
		auto descriptor_table_handle = cpu_base_handle;
		descriptor_table_handle.ptr += descriptor_table->descriptor_heap_offset * descriptor_size;
		
		for (auto& descriptor : descriptors) {
			switch (descriptor.type) {
			case HLSL::ResourceDescriptorType::Texture2D: {
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				desc.Format                        = DXGI_FORMAT_R16G16B16A16_FLOAT;
				desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.Texture2D.MostDetailedMip     = 0;
				desc.Texture2D.MipLevels           = 1;
				desc.Texture2D.PlaneSlice          = 0;
				desc.Texture2D.ResourceMinLODClamp = 0.f;
				
				device->CreateShaderResourceView(descriptor.texture.d3d12, &desc, descriptor_table_handle);
				break;
			} case HLSL::ResourceDescriptorType::RWTexture2D: {
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
				desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipSlice   = 0;
				desc.Texture2D.PlaneSlice = 0;
				
				context->device->CreateUnorderedAccessView(descriptor.texture.d3d12, nullptr, &desc, descriptor_table_handle);
				break;
			} case HLSL::ResourceDescriptorType::RegularBuffer: {
				
				// break;
			} case HLSL::ResourceDescriptorType::RWRegularBuffer: {
				
				// break;
			} default: {
				DebugAssertAlways("Unhandled ResourceDescriptorType '%u'.", (u32)descriptor.type);
			}
			}
			
			descriptor_table_handle.ptr += descriptor_size;
		}
	}
	
}




static void CmdClearRenderTargetD3D12(CmdClearRenderTargetPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
	command_list->ClearRenderTargetView({ packet->rtv_heap_index }, clear_color, 0, nullptr);
}

static void CmdSetRenderTargetsD3D12(CmdSetRenderTargetsPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->OMSetRenderTargets((u32)packet->rtv_heap_indices.count, (D3D12_CPU_DESCRIPTOR_HANDLE*)packet->rtv_heap_indices.data, false, nullptr);
}

static void CmdSetViewportAndScissorD3D12(CmdSetViewportAndScissorPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = (float)packet->min_x;
	viewport.TopLeftY = (float)packet->min_y;
	viewport.Width    = (float)(packet->max_x - packet->min_x);
	viewport.Height   = (float)(packet->max_y - packet->min_y);
	viewport.MinDepth = 0.f;
	viewport.MaxDepth = 1.f;
	command_list->RSSetViewports(1, &viewport);
	
	D3D12_RECT scissor = {};
	scissor.left   = (s32)packet->min_x;
	scissor.top    = (s32)packet->min_y;
	scissor.right  = (s32)packet->max_x;
	scissor.bottom = (s32)packet->max_y;
	command_list->RSSetScissorRects(1, &scissor);
}

static void CmdDispatchD3D12(CmdDispatchPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->Dispatch(packet->group_count_x, packet->group_count_y, packet->group_count_z);
}

static void CmdDrawInstancedD3D12(CmdDrawInstancedPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DrawInstanced(packet->vertex_count_per_instance, packet->instance_count, packet->start_vertex_location, packet->start_instance_location);
}

static void CmdDrawIndexedInstancedD3D12(CmdDrawIndexedInstancedPacket* packet, ID3D12GraphicsCommandList7* command_list) {
	command_list->DrawIndexedInstanced(packet->index_count_per_instance, packet->instance_count, packet->start_index_location, packet->base_vertex_location, packet->start_instance_location);
}


static void CmdSetRootSignatureD3D12(CmdSetRootSignaturePacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context) {
	command_list->SetComputeRootSignature(context->root_signature_table[packet->root_signature_index]);
}

static void CmdSetDescriptorTableD3D12(CmdSetDescriptorTablePacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context) {
	auto gpu_base_handle = context->gpu_base_handles[(u32)DescriptorHeapType::SRV].ptr;
	auto descriptor_size = context->descriptor_sizes[(u32)DescriptorHeapType::SRV];
	u64  descriptor_table = gpu_base_handle + packet->descriptor_heap_offset * descriptor_size;
	
	command_list->SetComputeRootDescriptorTable(packet->offset, { descriptor_table });
}

static void CmdSetPipelineStateD3D12(CmdSetPipelineStatePacket* packet, ID3D12GraphicsCommandList7* command_list, GraphicsContextD3D12* context) {
	command_list->SetPipelineState(context->pipeline_state_table[packet->pipeline_index]);
}


void ReplayRecordContext(GraphicsContext* api_context, RecordContext* record_context) {
	auto* context = (GraphicsContextD3D12*)api_context;
	FillDescriptorTables(context, record_context->descriptor_tables);
	
	auto* command_list = context->command_list;
	
	u32 command_count  = record_context->command_count;
	u8* command_memory = record_context->command_memory_base;
	for (u32 i = 0; i < command_count; i += 1) {
		auto* packet = (RecordContextCommandPacket*)command_memory;
		command_memory += packet->packet_size;
		
		switch (packet->packet_type) {
		case CommandType::Jump: command_memory = ((CmdJumpPacket*)packet)->command_memory; break;
		case CommandType::Dispatch:              CmdDispatchD3D12((CmdDispatchPacket*)packet, command_list); break;
		case CommandType::DrawInstanced:         CmdDrawInstancedD3D12((CmdDrawInstancedPacket*)packet, command_list); break;
		case CommandType::DrawIndexedInstanced:  CmdDrawIndexedInstancedD3D12((CmdDrawIndexedInstancedPacket*)packet, command_list); break;
		case CommandType::ClearRenderTarget:     CmdClearRenderTargetD3D12((CmdClearRenderTargetPacket*)packet, command_list); break;
		case CommandType::SetRenderTargets:      CmdSetRenderTargetsD3D12((CmdSetRenderTargetsPacket*)packet, command_list); break;
		case CommandType::SetViewportAndScissor: CmdSetViewportAndScissorD3D12((CmdSetViewportAndScissorPacket*)packet, command_list); break;
		case CommandType::SetRootSignature:      CmdSetRootSignatureD3D12((CmdSetRootSignaturePacket*)packet, command_list, context); break;
		case CommandType::SetDescriptorTable:    CmdSetDescriptorTableD3D12((CmdSetDescriptorTablePacket*)packet, command_list, context); break;
		case CommandType::SetPipelineState:      CmdSetPipelineStateD3D12((CmdSetPipelineStatePacket*)packet, command_list, context); break;
		default: DebugAssertAlways("Unhandled command packet type '%u'.", (u32)packet->packet_type); i = command_count; break;
		}
	}
}
