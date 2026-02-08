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
};

NOTES(Meta::HlslFile{ "MaterialData.hlsl"_sl })
struct GpuMaterialTextureData {
	u32 albedo = u32_max;
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

