#pragma once
#include "Basic/Basic.h"

enum struct CommandType : u16 {
	None                  = 0,
	Jump                  = 1,
	Dispatch              = 2,
	DrawInstanced         = 3,
	DrawIndexedInstanced  = 4,
	CopyBufferToTexture   = 5,
	ClearRenderTarget     = 6,
	ClearDepthStencil     = 7,
	SetRenderTargets      = 8,
	SetViewport           = 9,
	SetScissor            = 10,
	SetIndexBufferView    = 11,
	SetRootSignature      = 12,
	SetPipelineState      = 13,
	SetDescriptorTable    = 14,
	SetPushConstants      = 15,
	SetConstantBuffer     = 16,
	
	Count
};

struct RecordContextCommandPacket {
	CommandType packet_type = CommandType::None;
	u16 packet_size = 0;
};

struct CmdJumpPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::Jump;
	
	u8* command_memory = nullptr;
};

struct CmdDispatchPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::Dispatch;
	
	uint3 group_count;
};

struct CmdDrawInstancedPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::DrawInstanced;
	
	u32 vertex_count_per_instance = 0;
	u32 instance_count            = 0;
	u32 start_vertex_location     = 0;
	u32 start_instance_location   = 0;
};

struct CmdDrawIndexedInstancedPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::DrawIndexedInstanced;
	
	u32 index_count_per_instance = 0;
	u32 instance_count           = 0;
	u32 start_index_location     = 0;
	u32 base_vertex_location     = 0;
	u32 start_instance_location  = 0;
};

struct CmdCopyBufferToTexturePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::CopyBufferToTexture;
	
	GpuAddress        src_buffer_gpu_address  = {};
	VirtualResourceID dst_texture_resource_id = {};
	
	u32 src_row_pitch = 0;
	uint3 src_size = 0;
	
	u32 dst_subresource_index = 0;
	uint3 dst_offset = 0;
};

struct CmdClearRenderTargetPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::ClearRenderTarget;
	
	VirtualResourceID resource_id = {};
};

struct CmdClearDepthStencilPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::ClearDepthStencil;
	
	VirtualResourceID resource_id = {};
};

struct CmdSetRenderTargetsPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetRenderTargets;
	
	ArrayView<VirtualResourceID> resource_ids;
	VirtualResourceID depth_stencil_resource_id;
};

struct CmdSetViewportAndScissorPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::None;
	
	uint2 min = 0;
	uint2 max = 0;
};

struct CmdSetIndexBufferViewPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetIndexBufferView;
	
	GpuAddress gpu_address = {};
	u32 size = 0;
	TextureFormat format {};
};

struct CmdSetRootSignaturePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetRootSignature;
	
	RootSignatureID root_signature_id = { 0 };
	CommandQueueType pass_type = CommandQueueType::Graphics;
};

struct CmdSetPipelineStatePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetPipelineState;
	
	PipelineID pipeline_id;
};

struct CmdSetDescriptorTablePacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetDescriptorTable;
	
	u32 offset = 0;
	CommandQueueType pass_type = CommandQueueType::Graphics;
	u32 descriptor_heap_offset = 0;
};

struct CmdSetPushConstantsPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetPushConstants;
	
	u32 offset = 0;
	CommandQueueType pass_type = CommandQueueType::Graphics;
	ArrayView<u32> push_constants;
};

struct CmdSetConstantBufferPacket : RecordContextCommandPacket {
	compile_const CommandType my_type = CommandType::SetConstantBuffer;
	
	u32 offset = 0;
	CommandQueueType pass_type = CommandQueueType::Graphics;
	GpuAddress gpu_address = {};
};

