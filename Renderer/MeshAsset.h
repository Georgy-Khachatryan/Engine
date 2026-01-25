#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"


NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct BasicVertex {
	float3 position;
	float3 normal;
	float2 texcoord;
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
	u32 meshlet_header_offset = 0;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletHeader {
	u32 triangle_count = 0;
	u32 vertex_count   = 0;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletPageHeader {
	compile_const u32 page_size = 64 * 1024; // TODO: Experiment with different page sizes.
	compile_const u32 max_page_count = 256;
	
	u32 meshlet_count = 0;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct MeshletGroup {
	u32 meshlet_offset = 0;
	u16 meshlet_count  = 0;
	u16 is_resident    = 0;
	u32 page_index = 0;
	u32 page_count = 0;
	
	MeshletErrorMetric error_metric;
	
	compile_const u32 offset_of_is_resident = 6;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct GpuMeshAssetData {
	u32 page_buffer_offset  = 0;
	u32 meshlet_group_buffer_offset = 0;
	u32 meshlet_group_count = 0;
};

NOTES()
struct MeshSourceData {
	String filepath;
};

NOTES()
struct MeshRuntimeDataLayout {
	compile_const u64 current_version = 15;
	
	u64 file_guid = 0;
	u64 version   = 0;
	
	u32 page_count = 0;
	u32 meshlet_group_count = 0;
	
	u32 PageBufferOffset() { return 0; }
	u32 MeshletGroupBufferOffset() { return PageBufferOffset() + page_count * MeshletPageHeader::page_size; }
	u32 PageResidencyMaskOffset()  { return MeshletGroupBufferOffset() + meshlet_group_count * sizeof(MeshletGroup); }
	u32 AllocationSize() { return PageResidencyMaskOffset() + meshlet_group_count * (MeshletPageHeader::max_page_count / 8u); }
};

NOTES(Meta::NoSaveLoad{})
struct MeshRuntimeFile {
	FileHandle file;
};

NOTES(Meta::NoSaveLoad{})
struct MeshRuntimeAllocation {
	u32 base_offset = u32_max;
	u32 streamed_in_page_count = 0;
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
	
	NOTES(VirtualResourceID::GpuMeshAssetData)
	ECS::GpuComponent<GpuMeshAssetData> gpu_mesh_asset_data;
};

