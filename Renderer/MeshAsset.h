#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "MaterialAsset.h"


NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletVertex {
	u16x3 position;
	s16x2 normal;
	s16x2 tangent;
	float16x2 texcoord;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletErrorMetric {
	float3 center = 0.f;
	float  radius = 0.f;
	float  error  = 0.f;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletCullingData {
	MeshletErrorMetric current_level_error_metric;
	
	float3 aabb_center;
	float3 aabb_radius;
	
	u32 meshlet_header_offset = 0;
	u32 current_level_meshlet_group_index = u32_max;
	u32 level_of_detail_index = 0;
	float world_to_uv_scale = 0.f;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletHeader {
	float3 position_offset;
	u16 triangle_count = 0;
	u16 vertex_count   = 0;
	u32 level_of_detail_index = 0;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletPageHeader {
	compile_const u32 page_size = 128 * 1024; // TODO: Experiment with different page sizes.
	compile_const u32 max_page_count = 2048;
	compile_const u32 runtime_page_count = 1024;
	
	u32 meshlet_count = 0;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletGroup {
	MeshletErrorMetric error_metric;
	
	float3 aabb_center;
	float3 aabb_radius;
	
	u32 meshlet_offset = 0;
	u16 meshlet_count  = 0;
	u16 is_resident    = 0;
	u32 page_index = 0;
	u32 page_count = 0;
	
	compile_const u32 offset_of_is_resident = 50;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct GpuMeshAssetData {
	u32 meshlet_group_buffer_offset = 0;
	u16 meshlet_group_count = 0;
	u16 meshlet_page_count  = 0;
	u32 feedback_buffer_offset = 0; // GPU allocated every frame.
	
	float3 aabb_center;
	float3 aabb_radius;
	
	float rcp_quantization_scale = 1.f;
};

NOTES()
struct MeshSourceData {
	String filepath;
};

NOTES()
struct MeshRuntimeDataLayout {
	compile_const u64 current_version = 32;
	
	u64 file_guid = 0;
	u64 version   = 0;
	
	u32 page_count = 0;
	u32 meshlet_group_count = 0;
	u32 meshlet_count = 0;
	
	float rcp_quantization_scale = 1.f;
};

struct MeshImportResult {
	MeshRuntimeDataLayout layout;
	float3 aabb_min;
	float3 aabb_max;
	bool success = false;
};

NOTES(Meta::NoSaveLoad{})
struct MeshRuntimeFile {
	FileHandle file;
};

NOTES(Meta::NoSaveLoad{})
struct MeshRuntimeAllocation {
	NumaHeapAllocation allocation;
	u32 offset = u32_max;
};

NOTES()
struct MeshAssetGUID {
	u64 guid = 0;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct MeshAssetType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<MeshSourceData>        source_data;
	ECS::Component<MeshRuntimeDataLayout> runtime_data_layout;
	ECS::Component<MeshRuntimeFile>       runtime_file;
	ECS::Component<MeshRuntimeAllocation> allocation;
	ECS::Component<AabbComponent>         aabb;
	ECS::Component<MaterialAssetGUID>     material_asset;
	
	NOTES(VirtualResourceID::GpuMeshAssetData)
	ECS::GpuComponent<GpuMeshAssetData> gpu_mesh_asset_data;
	
	NOTES(VirtualResourceID::MeshAssetAliveMask)
	ECS::GpuMaskComponent<AliveEntityMask> alive_mask;
};

