#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

//
// Driving Toward Reality: Physically Based Tone Mapping and Perceptual Fidelity in Gran Turismo 7
// See license in THIRD_PARTY_LICENSES.md
//
static ToneMappingGpuConstants InitializeToneMappingGpuConstants(const ToneMappingSettings& settings) {
	ToneMappingGpuConstants constants;
	
	constants.method = settings.method;
	
	if (settings.method == ToneMappingMethod::GT7_HDR) {
		constants.framebuffer_luminance_target = settings.physical_target_luminance_hdr / ToneMappingGpuConstants::reference_luminance;
		constants.sdr_correction_factor        = 1.f;
	} else if (settings.method == ToneMappingMethod::GT7_SDR) {
		constants.framebuffer_luminance_target = settings.physical_target_luminance_sdr / ToneMappingGpuConstants::reference_luminance;
		constants.sdr_correction_factor        = 1.f / constants.framebuffer_luminance_target;
	}
	
	constants.mid_point     = settings.mid_point;
	constants.toe_threshold = settings.linear_section * constants.framebuffer_luminance_target;
	constants.toe_power     = settings.toe_power;
	
	// Precompute constants for the shoulder region.
	float k = (settings.linear_section - 1.f) / (settings.alpha - 1.f);
	constants.k_a = constants.framebuffer_luminance_target * settings.linear_section + constants.framebuffer_luminance_target * k;
	constants.k_b = -constants.framebuffer_luminance_target * k * expf(settings.linear_section / k);
	constants.k_c = -1.f / (k * constants.framebuffer_luminance_target);
	
	constants.blend_ratio = settings.blend_ratio;
	constants.fade_start  = settings.fade_start;
	constants.fade_end    = settings.fade_end;
	
	return constants;
}

void ToneMappingRenderPass::CreatePipelines(PipelineLibrary* lib) {
	pipeline_id = CreateComputePipeline(lib, ToneMappingShadersID);
}

void ToneMappingRenderPass::RecordPass(RecordContext* record_context) {
	auto& descriptor_table = AllocateDescriptorTable(record_context, root_signature.descriptor_table);
	descriptor_table.scene_radiance = scene_radiance;
	
	auto constants = InitializeToneMappingGpuConstants(tone_mapping_settings);
	
	auto [gpu_address, cpu_address] = AllocateTransientUploadBuffer<ToneMappingGpuConstants>(record_context);
	memcpy(cpu_address, &constants, sizeof(constants));
	
	CmdSetRootSignature(record_context, root_signature);
	CmdSetRootArgument(record_context, root_signature.descriptor_table, descriptor_table);
	CmdSetRootArgument(record_context, root_signature.constants, gpu_address);
	CmdSetPipelineState(record_context, pipeline_id);
	
	auto render_target_size = GetTextureSize(record_context, VirtualResourceID::SceneRadiance);
	CmdDispatch(record_context, DivideAndRoundUp(uint2(render_target_size), 16u));
}
