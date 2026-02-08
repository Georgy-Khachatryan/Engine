#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicString.h"
#include "EntitySystem/EntitySystem.h"
#include "EntitySystem/Components.h"
#include "GraphicsApi/GraphicsApiTypes.h"


NOTES()
struct TextureSourceData {
	String filepath;
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
	u64 file_read_wait_index = 0;
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
