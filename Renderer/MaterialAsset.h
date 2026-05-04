#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "TextureAsset.h"


NOTES()
struct MaterialTextureData {
	TextureAssetGUID albedo;
	TextureAssetGUID normal;
	TextureAssetGUID roughness;
	TextureAssetGUID metalness;
	
	u32     default_albedo    = 0x3FFFFFFF; // (1, 1, 1), R10G10B10_UNORM, linear rec709.
	float16 default_roughness = 0x3800;     // 0.5,       R16_FLOAT,       linear.
	float16 default_metalness = 0x0000;     // 0.0,       R16_FLOAT,       linear.
};

NOTES(Meta::HlslFile{ "MaterialData.hlsl"_sl })
struct GpuMaterialTextureData {
	u32 albedo    = u32_max;
	u32 normal    = u32_max;
	u32 roughness = u32_max;
	u32 metalness = u32_max;
};

NOTES(Meta::HlslFile{ "MaterialData.hlsl"_sl })
enum struct MaterialTextureIndexFlags : u32 {
	None       = 0,
	UseDefault = 1u << 31,
};

NOTES()
struct MaterialAssetGUID {
	u64 guid = 0;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct MaterialAssetType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<MaterialTextureData> texture_data;
	
	NOTES(VirtualResourceID::MaterialAssetTextureData)
	ECS::GpuComponent<GpuMaterialTextureData> gpu_texture_data;
};

