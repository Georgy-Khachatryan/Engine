#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "GraphicsApi/GraphicsApiTypes.h"


NOTES()
enum struct TextureAssetTargetEncoding : u32 {
	BC1_UNORM_SRGB = 0,
	BC1_UNORM      = 1,
	BC4_UNORM      = 2,
	BC5_UNORM      = 3,
	BC5_NORMAL_MAP = 4,
	// TODO: BC6H, BC7
	Count
};

NOTES()
struct TextureSourceData {
	String filepath;
	
	TextureAssetTargetEncoding target_encoding = TextureAssetTargetEncoding::BC1_UNORM_SRGB;
};

NOTES()
struct TextureRuntimeDataLayout {
	compile_const u64 current_version = 1;
	
	u64 file_guid = 0;
	u64 version   = 0;
	
	TextureSize size;
};

struct TextureImportResult {
	TextureRuntimeDataLayout layout;
	bool success = false;
};

NOTES(Meta::NoSaveLoad{})
struct TextureRuntimeFile {
	FileHandle file;
};

NOTES(Meta::NoSaveLoad{})
struct TextureDescriptorAllocation {
	u32 index = u32_max;
};

NOTES(Meta::NoSaveLoad{})
struct TextureRuntimeAllocation {
	NativeTextureResource resource;
	SparseTextureLayout sparse_layout;
};

NOTES()
struct TextureAssetGUID {
	u64 guid = 0;
};

NOTES(Meta::EntityType{}, Meta::ComponentQuery{})
struct TextureAssetType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<TextureSourceData>        source_data;
	ECS::Component<TextureRuntimeDataLayout> runtime_data_layout;
	ECS::Component<TextureRuntimeFile>       runtime_file;
	ECS::Component<TextureDescriptorAllocation> descriptor_allocation;
	ECS::Component<TextureRuntimeAllocation>    resource_allocation;
};
