#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "Components.h"
#include "EntitySystem.h"


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
struct BasicMeshlet {
	u32 index_buffer_offset  = 0;
	u32 vertex_buffer_offset = 0;
	u32 triangle_count = 0;
	u32 vertex_count   = 0;
	
	MeshletErrorMetric current_level_error_metric;
	MeshletErrorMetric coarser_level_error_metric;
};

NOTES(Meta::HlslFile{ "MeshData.hlsl"_sl })
struct GpuMeshAssetData {
	u32 vertex_buffer_offset  = 0;
	u32 meshlet_buffer_offset = 0;
	u32 index_buffer_offset   = 0;
	u32 meshlet_count         = 0;
};

NOTES()
struct MeshSourceData {
	String filepath;
};

NOTES()
struct MeshRuntimeDataLayout {
	u64 file_guid = 0;
	
	u32 vertex_count  = 0;
	u32 meshlet_count = 0;
	u32 indices_count = 0;
	
	u32 VertexBufferOffset()  { return 0; }
	u32 MeshletBufferOffset() { return VertexBufferOffset()  + vertex_count  * sizeof(BasicVertex);  }
	u32 IndexBufferOffset()   { return MeshletBufferOffset() + meshlet_count * sizeof(BasicMeshlet); }
	u32 AllocationSize()      { return IndexBufferOffset()   + indices_count * sizeof(u8);           }
};

NOTES(Meta::NoSaveLoad{})
struct MeshRuntimeFile {
	FileHandle file;
};

NOTES(Meta::NoSaveLoad{})
struct MeshRuntimeAllocation {
	u32 base_offset = 0;
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

