#include "RenderPasses.h"
#include "GraphicsApi/GraphicsApi.h"
#include "GraphicsApi/RecordContext.h"

void DlssRenderPass::RecordPass(RecordContext* record_context) {
	DlssDispatchContext context;
	context.dlss_handle_resource_id   = VirtualResourceID::DlssHandle;
	context.result_resource_id        = VirtualResourceID::SceneRadianceResult;
	context.radiance_resource_id      = VirtualResourceID::SceneRadiance;
	context.depth_resource_id         = VirtualResourceID::DepthStencil;
	context.motion_vector_resource_id = VirtualResourceID::MotionVectors;
	context.exposure_texture          = VirtualResourceID::ExposureTexture;
	context.jitter_offset_pixels      = jitter_offset_pixels;
	context.exposure_estimate         = exposure_estimate;
	CmdDispatchDLSS(record_context, context);
}

void XessRenderPass::RecordPass(RecordContext* record_context) {
	XessDispatchContext context;
	context.xess_handle_resource_id   = VirtualResourceID::XessHandle;
	context.result_resource_id        = VirtualResourceID::SceneRadianceResult;
	context.radiance_resource_id      = VirtualResourceID::SceneRadiance;
	context.depth_resource_id         = VirtualResourceID::DepthStencil;
	context.motion_vector_resource_id = VirtualResourceID::MotionVectors;
	context.exposure_texture          = VirtualResourceID::ExposureTexture;
	context.jitter_offset_pixels      = jitter_offset_pixels;
	context.exposure_estimate         = exposure_estimate;
	CmdDispatchXeSS(record_context, context);
}
