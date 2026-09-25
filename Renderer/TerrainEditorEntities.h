#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "EntitySystem/Components.h"
#include "EntitySystem/EntitySystem.h"


compile_const String terrain_editor_data_filename = "TerrainEditorData.hlsl"_sl;

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
enum struct TerrainEditorCommandType : u32 {
	None  = 0,
	Clear = 1,
	Copy  = 2,
	
	TerrainHeightLayerNoise      = 3,
	TerrainHeightLayerDistortion = 4,
	TerrainHeightLayerStrata     = 5,
	
	Count
};


NOTES(Meta::HlslFile{ terrain_editor_data_filename })
enum struct TerrainEditorNoiseType : u32 {
	Gradient         = 0,
	GradientBillow   = 1,
	Value            = 2,
	VoronoiF1        = 3,
	VoronoiF2        = 4,
	VoronoiF2MinusF1 = 5,
	VoronoiID        = 6,
	
	Count
};

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
enum struct TerrainEditorDistortionType : u32 {
	None     = 0,
	Gradient = 1,
	
	Count
};


NOTES()
struct TerrainHeightLayerNoiseCpuSettings {
	TerrainEditorNoiseType type = TerrainEditorNoiseType::Gradient;
	u32 random_seed = 0;
	
	float scale      = 256.f; // XY scale.
	float anisotropy = 0.f;   // XY scale anisotropy.
	float rotation   = 0.f;   // XY rotation.
	float amplitude  = 256.f; // Z  scale.
	
	u32 octave_count = 8;
	float lacunarity = 2.0f; // XY scale for each subsequent octave.
	float gain       = 0.5f; // Z  scale for each subsequent octave.
	
	TerrainEditorDistortionType distortion_type = TerrainEditorDistortionType::None;
	float distortion_scale      = 32.f;
	float distortion_amplitude  = 32.f;
	u32 distortion_octave_count = 6;
};

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
struct TerrainHeightLayerNoiseGpuSettings {
	TerrainEditorNoiseType type = TerrainEditorNoiseType::Gradient;
	u32 random_seed = 0;
	
	float inv_scale  = 0.f;
	float anisotropy = 0.f;
	float2 rotation  = 0.f;
	float amplitude  = 0.f;
	
	u32 octave_count = 0;
	float lacunarity = 0.f;
	float gain       = 0.f;
	
	TerrainEditorDistortionType distortion_type = TerrainEditorDistortionType::None;
	float inv_distortion_scale  = 0.f;
	float distortion_amplitude  = 0.f;
	u32 distortion_octave_count = 0;
};

NOTES(Meta::EntityType{ 16 }, Meta::ComponentQuery{})
struct TerrainHeightLayerNoiseEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	ECS::Component<HierarchyComponent> hierarchy;
	
	ECS::Component<TerrainHeightLayerNoiseCpuSettings> settings;
};


NOTES()
struct TerrainHeightLayerDistortionCpuSettings {
	TerrainEditorNoiseType type = TerrainEditorNoiseType::Gradient;
	u32 random_seed = 0;
	
	float scale      = 128.f; // XY scale.
	float anisotropy = 0.f;   // XY scale anisotropy.
	float rotation   = 0.f;   // Rotation around Z.
	float amplitude  = 64.f;  // XY distortion scale.
	
	u32 octave_count = 8;
	float lacunarity = 2.0f; // XY scale for each subsequent octave.
	float gain       = 0.5f; // Z  scale for each subsequent octave.
};

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
struct TerrainHeightLayerDistortionGpuSettings {
	TerrainEditorNoiseType type = TerrainEditorNoiseType::Gradient;
	u32 random_seed = 0;
	
	float inv_scale  = 0.f;
	float anisotropy = 0.f;
	float2 rotation  = 0.f;
	float amplitude  = 0.f;
	
	u32 octave_count = 0;
	float lacunarity = 0.f;
	float gain       = 0.f;
};

NOTES(Meta::EntityType{ 16 }, Meta::ComponentQuery{})
struct TerrainHeightLayerDistortionEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	ECS::Component<HierarchyComponent> hierarchy;
	
	ECS::Component<TerrainHeightLayerDistortionCpuSettings> settings;
};


NOTES()
struct TerrainHeightLayerStrataCpuSettings {
	u32 random_seed = 0;
	
	float period     = 16.f;  // Max strata slice period.
	float tilt       = 0.f;   // Rotation around X.
	float rotation   = 0.f;   // Rotation around Z.
	float randomness = 1.f;   // Slice thickness randomness.
	float amount     = 1.f;   // Overall amount.
	
	float distortion_scale  = 2.f;   // Slice distortion XY scale.
	float distortion_amount = 0.25f; // Slice distortion amount.
	
	u32 octave_count = 5;
	float lacunarity = 2.f; // XY scale for each subsequent octave.
	float gain       = 1.f; // Z  scale for each subsequent octave.
};

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
struct TerrainHeightLayerStrataGpuSettings {
	u32 random_seed = 0;
	
	float  inv_period = 0.f;
	float2 tilt       = 0.f;
	float2 rotation   = 0.f;
	float  randomness = 0.f;
	float  amount     = 0.f;
	
	float inv_distortion_scale = 0.f;
	float distortion_amount    = 0.f;
	
	u32 octave_count = 0;
	float lacunarity = 0.f;
	float gain       = 0.f;
};

NOTES(Meta::EntityType{ 16 }, Meta::ComponentQuery{})
struct TerrainHeightLayerStrataEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	ECS::Component<HierarchyComponent> hierarchy;
	
	ECS::Component<TerrainHeightLayerStrataCpuSettings> settings;
};


NOTES(Meta::EntityType{ 4 }, Meta::ComponentQuery{})
struct TerrainEditorLayerStackEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<HierarchyComponent> hierarchy;
};

NOTES(Meta::ComponentQuery{})
struct TerrainEditorLayerQuery {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<HierarchyComponent> hierarchy;
};

NOTES(Meta::ComponentQuery{})
struct TerrainEditorLayerSettingsQuery {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	TerrainHeightLayerNoiseCpuSettings*      noise_cpu_settings      = nullptr;
	TerrainHeightLayerDistortionCpuSettings* distortion_cpu_settings = nullptr;
	TerrainHeightLayerStrataCpuSettings*     strata_cpu_settings     = nullptr;
};
