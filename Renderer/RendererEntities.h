#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct DebugMeshInstanceArray;
struct DebugGeometryBuffer;

NOTES(Meta::HlslFile{ "SceneData.hlsl"_sl })
struct SceneConstants {
	float2 render_target_size;
	float2 inv_render_target_size;
	
	float2 prev_render_target_size;
	float2 inv_prev_render_target_size;
	
	float4 view_to_clip_coef;
	float4 clip_to_view_coef;
	float3x4 view_to_world;
	float3x4 world_to_view;
	
	float4 prev_view_to_clip_coef;
	float4 prev_clip_to_view_coef;
	float3x4 prev_view_to_world;
	float3x4 prev_world_to_view;
	
	float2 culling_hzb_size;
	float2 inv_culling_hzb_size;
	
	// Separate from the regular camera transform because we might want to freeze these values for debugging.
	float3x4 culling_world_to_view;
	float4 culling_view_to_clip_coef;
	float3x4 culling_prev_world_to_view;
	float4 culling_prev_view_to_clip_coef;
	
	float2 jitter_offset_pixels;
	float2 jitter_offset_ndc;
	
	float meshlet_world_to_pixel_scale; // Used for meshlet LOD error computation.
	float3 world_space_camera_position;
	
	float texture_world_to_pixel_scale; // Used for texture streaming feedback.
	u32 frame_index = 0;
	float reference_path_tracer_percent;
	u32 path_tracer_accumulated_frame_count = 0;
	
	float exposure_estimate     = 0.f;
	float inv_exposure_estimate = 0.f;
	u32 global_light_entity_index = 0;
	u32 padding;
};

NOTES(Meta::HlslFile{ "ToneMappingData.hlsl"_sl })
enum struct ExposureMethod : u32 {
	Manual    = 0,
	Automatic = 1,
	
	Count
};

NOTES()
struct ExposureSettings {
	compile_const u32 histogram_bucket_count = 256;
	compile_const u32 exposure_buffer_size   = 2;
	
	float manual_exposure_offset_ev    = 0.f;
	float automatic_exposure_offset_ev = 0.f;
	
	float histogram_min_ev = -16.f;
	float histogram_max_ev = +16.f;
	
	float histogram_min_cutoff = 0.5f; // Ignore 50% of the dimmest pixels.
	float histogram_max_cutoff = 0.9f; // Ignore 10% of the brightest pixels.
	
	// Time it takes to change exposure half way, in seconds.
	float exposure_increase_half_time = 1.f;
	float exposure_decrease_half_time = 1.f;
	
	float exposure_min_ev = -4.f;
	float exposure_max_ev = +4.f;
	
	ExposureMethod method = ExposureMethod::Manual;
};

NOTES(Meta::HlslFile{ "ToneMappingData.hlsl"_sl })
enum struct ToneMappingMethod : u32 {
	None         = 0,
	GT7_HDR      = 1,
	GT7_SDR      = 2,
	Reinhard_SDR = 3,
	
	Count
};

NOTES()
struct ToneMappingSettings {
	float physical_target_luminance_hdr = 500.f; // cd/m^2
	float physical_target_luminance_sdr = 250.f; // cd/m^2, SDR Paper White
	
	float alpha          = 0.25f;
	float mid_point      = 0.538f;
	float linear_section = 0.444f;
	float toe_power      = 1.28f;
	
	float blend_ratio    = 0.6f;
	float fade_start     = 0.98f;
	float fade_end       = 1.16f;
	
	ToneMappingMethod method = ToneMappingMethod::GT7_HDR;
};

NOTES()
enum struct AntiAliasingMethod : u32 {
	None = 0,
	DLSS = 1,
	XeSS = 2,
	
	Count
};

NOTES()
struct AntiAliasingSettings {
	AntiAliasingMethod method = AntiAliasingMethod::DLSS;
};


struct DebugFreezeCullingCamera {
	bool enabled = false;
	quat view_to_world_rotation;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletCullingStatistics {
	u32 meshlet_count                   = 0;
	u32 meshlet_count_main_pass         = 0;
	u32 meshlet_count_disocclusion_pass = 0;
	u32 meshlet_count_raytracing_pass   = 0;
};

struct AutomaticExposureHistogram {
	compile_const u32 histogram_bucket_count = ExposureSettings::histogram_bucket_count;
	
	float histogram[histogram_bucket_count];
	float final_ev = 0.f;
	float final_exposure = 1.f;
};

NOTES(Meta::SaveLoadOptions{ SaveLoadFlags::None })
struct RendererWorld {
	SceneConstants scene_constants;
	
	float2 window_size = float2(1.f, 1.f);
	float  delta_time  = 0.f;
	
	float meshlet_target_error_pixels = 1.f;
	float reference_path_tracer_percent = 0.f;
	bool  reset_reference_path_tracer   = false;
	
	u32 scene_descriptor_heap_offset = 0; // Descriptor index for the final image.
	
	DebugFreezeCullingCamera debug_freeze_culling_camera;
	MeshletCullingStatistics meshlet_culling_statistics;
	AutomaticExposureHistogram automatic_exposure_histogram;
	
	GpuReadbackQueue meshlet_streaming_feedback_queue;
	GpuReadbackQueue mesh_streaming_feedback_queue;
	GpuReadbackQueue texture_streaming_feedback_queue;
	GpuReadbackQueue meshlet_culling_statistics_readback_queue;
	GpuReadbackQueue automatic_exposure_readback_queue;
	
	ArrayView<DebugMeshInstanceArray> debug_mesh_instance_arrays;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct GpuTransform {
	float3 position;
	float scale;
	quat rotation;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct GpuMeshEntityData {
	u32 mesh_asset_index     = 0;
	u32 material_asset_index = 0;
};


NOTES()
enum struct CameraTransformType : u32 {
	Perspective  = 0,
	Orthographic = 1,
};

NOTES()
struct CameraComponent {
	float vertical_fov_degrees = 75.f;
	float near_depth           = 0.1f;
	CameraTransformType transform_type = CameraTransformType::Perspective;
};

NOTES()
struct CameraEntityGUID {
	u64 guid = 0;
};

NOTES(Meta::HlslFile{ "LightData.hlsl"_sl })
enum struct LightType : u32 {
	Spot   = 0,
	Point  = 1,
	Global = 2,
};

NOTES()
struct LightComponent {
	LightType type = LightType::Spot;
	float3   color = 1.f; // SRGB rec709.
	
	union {
		float radiance_or_irradiance = 1.f;
		
		float radiance;   // Used for Spot and Point lights, W
		float irradiance; // Used for Global lights, W/m^2
	};
	
	float radius = 0.03f; // Used for Spot and Point lights (Default is 60mm diameter E27 light bulb).
	
	float inner_attenuation_radius = 9.f;
	float outer_attenuation_radius = 10.f;
	
	float inner_attenuation_angle = 60.f * Math::degrees_to_radians;
	float outer_attenuation_angle = 90.f * Math::degrees_to_radians;
};

NOTES(Meta::HlslFile{ "LightData.hlsl"_sl })
struct GpuLightEntityData {
	float3 light_position             = 0.f;
	float3 light_direction            = 0.f;
	float3 color                      = 1.f; // Linear Rec709.
	float radiance_or_irradiance      = 0.f;
	LightType type                    = LightType::Spot;
	float radius                      = 0.f;
	float inner_attenuation_radius    = 0.f;
	float outer_attenuation_radius    = 0.f;
	float cos_inner_attenuation_angle = 0.f;
	float cos_outer_attenuation_angle = 0.f;
};

NOTES()
struct LightEntityGUID {
	u64 guid = 0;
};


compile_const String debug_geometry_data_filename = "DebugGeometryData.hlsl"_sl;

NOTES(Meta::HlslFile{ debug_geometry_data_filename })
enum struct DebugMeshInstanceType : u32 {
	Sphere   = 0,
	Cube     = 1,
	Cylinder = 2,
	Torus    = 3,
	
	Count
};

NOTES(Meta::HlslFile{ debug_geometry_data_filename })
struct DebugMeshInstance {
	float3 position;
	u32   color = u32_max;
	s16x4 rotation;
	float16x4 packed_data;
};

struct DebugMeshInstanceArray {
	DebugMeshInstanceType instance_type = DebugMeshInstanceType::Sphere;
	ArrayView<DebugMeshInstance> debug_mesh_instances;
};


NOTES(Meta::ComponentQuery{})
struct WorldEntityQuery {
	ECS::Component<LightEntityGUID>      global_light_entity;
	ECS::Component<CameraEntityGUID>     camera_entity;
	ECS::Component<RendererWorld>        renderer_world;
	ECS::Component<AntiAliasingSettings> anti_aliasing_settings;
	ECS::Component<ExposureSettings>     exposure_settings;
	ECS::Component<ToneMappingSettings>  tone_mapping_settings;
	ECS::GpuComponent<SceneConstants>    gpu_scene_constants;
};

NOTES(Meta::ComponentQuery{})
struct GpuMeshEntityQuery {
	ECS::GpuComponent<GpuMeshEntityData> gpu_mesh_entity_data;
};

NOTES(Meta::ComponentQuery{})
struct CameraEntityQuery {
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<CameraComponent>   camera;
};

NOTES(Meta::ComponentQuery{})
struct LightEntityQuery {
	ECS::Component<PositionComponent> position;
	ECS::Component<RotationComponent> rotation;
	ECS::Component<LightComponent>    light;
};

struct RecordContext;
struct AsyncTransferQueue;
struct ThreadPool;
struct GpuComponentUploadBuffer;
void UpdateRendererEntityGpuComponents(StackAllocator* alloc, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads);
void ReleaseTextureAssets(StackAllocator* alloc, GraphicsContext* graphics_context, AssetEntitySystem& asset_system);
