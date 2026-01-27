#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void BuildHzbRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, BuildHzbShadersID);
}

void BuildHzbRenderPass::RecordPass(RecordContext* record_context) {
	auto culling_hzb_size = GetTextureSize(record_context, VirtualResourceID::CullingHZB);
	
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	for (u32 i = 0; i < (u32)culling_hzb_size.mips; i += 1) {
		descriptor_table.culling_hzb[i].Bind(VirtualResourceID::CullingHZB, i);
	}
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto thread_group_count = DivideAndRoundUp(uint2(culling_hzb_size), 32u);
	CmdSetRootArgument(record_context, root_signature.constants, { thread_group_count.x * thread_group_count.y - 1 });
	
	CmdDispatch(record_context, thread_group_count);
}

TextureSize BuildHzbRenderPass::ComputeCullingHzbSize(uint2 render_target_size) {
	TextureSize size;
	size.x = Math::Max(RoundUpToPowerOfTwo32(render_target_size.x) >> 2u, 1u);
	size.y = Math::Max(RoundUpToPowerOfTwo32(render_target_size.y) >> 2u, 1u);
	size.z = 1u;
	size.mips = Math::Min(FirstBitHigh32(Math::Max(size.x, size.y)), culling_hzb_max_mip_count);
	size.format = TextureFormat::R32_FLOAT;
	
	return size;
}
