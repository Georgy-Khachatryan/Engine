#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "GraphicsApi/GraphicsApiTypes.h"

struct GpuComponentUploadBuffer;
struct DebugMeshInstanceArray;
struct DebugGeometryBuffer;

NOTES(Meta::HlslFile{ "SceneData.hlsl"_sl }, Meta::NoSaveLoad{})
struct SceneConstants {
	float2 render_target_size;
	float2 inv_render_target_size;
	
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
	u32 frame_index;
	float reference_path_tracer_percent;
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

NOTES(Meta::NoSaveLoad{})
struct RendererWorld {
	SceneConstants scene_constants;
	
	float2 window_size = float2(1.f, 1.f);
	float sun_elevation_degrees = 3.f;
	float meshlet_target_error_pixels = 1.f;
	float reference_path_tracer_percent = 0.f;
	
	DebugFreezeCullingCamera debug_freeze_culling_camera;
	MeshletCullingStatistics meshlet_culling_statistics;
	
	bool enable_anti_aliasing = true;
	u32 jitter_frame_index = 0;
	
	GpuReadbackQueue meshlet_streaming_feedback_queue;
	GpuReadbackQueue mesh_streaming_feedback_queue;
	GpuReadbackQueue texture_streaming_feedback_queue;
	GpuReadbackQueue meshlet_culling_statistics_readback_queue;
	
	Array<GpuComponentUploadBuffer> gpu_uploads;
	
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
	ECS::Component<CameraEntityGUID> camera_entity;
	ECS::Component<RendererWorld> renderer_world;
	ECS::GpuComponent<SceneConstants> gpu_scene_constants;
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

struct RecordContext;
struct AsyncTransferQueue;
struct ThreadPool;
void UpdateRendererEntityGpuComponents(StackAllocator* alloc, ThreadPool* thread_pool, AsyncTransferQueue* async_transfer_queue, RecordContext* record_context, AssetEntitySystem& asset_system, Array<GpuComponentUploadBuffer>& gpu_uploads);
void ReleaseTextureAssets(StackAllocator* alloc, GraphicsContext* graphics_context, AssetEntitySystem& asset_system);
