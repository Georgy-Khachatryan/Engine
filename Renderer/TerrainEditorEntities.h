#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "EntitySystem/Components.h"
#include "EntitySystem/EntitySystem.h"


compile_const String terrain_editor_data_filename = "TerrainEditorData.hlsl"_sl;

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
enum struct TerrainEditorCommandType : u32 {
	None = 0,
	
	Clear,
	Copy,
	Upscale,
	
	TerrainHeightLayerNoise,
	TerrainHeightLayerDistortion,
	TerrainHeightLayerStrata,
	
	TerrainHeightLayerErosionClear,
	TerrainHeightLayerErosionSimulate,
	TerrainHeightLayerErosionApply,
	
	Count
};

enum struct TerrainEditorEqualizerPreset : u32 {
	None           = 0,
	Neutral        = 1,
	LowPass        = 2,
	HighPass       = 3,
	Linear         = 4,
	InverseLinear  = 5,
	VShaped        = 6,
	InverseVShaped = 7,
	UShaped        = 8,
	InverseUShaped = 9,
	
	Count
};

NOTES()
struct TerrainEditorEqualizer {
	compile_const s32 min_frequency_band = -2;
	compile_const s32 max_frequency_band = +9;
	compile_const s32 frequency_band_count = max_frequency_band - min_frequency_band + 1;
	
	FixedCountArray<float, frequency_band_count> bands;
	float scale = 1.f;
	
	float& operator[] (s32 index) { return bands[index - min_frequency_band]; }
	
	float NormalizedBandScale(s32 index) const {
		float sum = 0.f;
		for (float v : bands) sum += v;
		return bands[index - min_frequency_band] * scale * (sum > 0.f ? 1.f / sum : 0.f);
	}
	
	float AdditiveBandScale(s32 index) const {
		return bands[index - min_frequency_band] * scale;
	}
	
	static TerrainEditorEqualizer MakePreset(TerrainEditorEqualizerPreset preset, float scale);
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
	
	TerrainEditorEqualizer amount = TerrainEditorEqualizer::MakePreset(TerrainEditorEqualizerPreset::Neutral, 0.125f);
	
	float scale      = 256.f; // XY scale.
	float anisotropy = 0.f;   // XY scale anisotropy.
	float rotation   = 0.f;   // XY rotation.
	float amplitude  = 0.75f; // Z  scale relative to XY scale.
	
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
	
	TerrainEditorEqualizer amount = TerrainEditorEqualizer::MakePreset(TerrainEditorEqualizerPreset::Neutral, 0.125f);
	
	float scale      = 128.f; // XY scale.
	float anisotropy = 0.f;   // XY scale anisotropy.
	float rotation   = 0.f;   // Rotation around Z.
	float amplitude  = 0.5f;  // XY distortion scale relative the the scale.
	
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
	
	TerrainEditorEqualizer amount = TerrainEditorEqualizer::MakePreset(TerrainEditorEqualizerPreset::Neutral, 0.125f);
	
	float period     = 16.f;  // Max strata slice period.
	float tilt       = 0.f;   // Rotation around X.
	float rotation   = 0.f;   // Rotation around Z.
	float randomness = 1.f;   // Slice thickness randomness.
	
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


NOTES()
struct TerrainHeightLayerErosionCpuSettings {
	u32   random_seed = 0;
	
	TerrainEditorEqualizer amount = TerrainEditorEqualizer::MakePreset(TerrainEditorEqualizerPreset::Neutral, 0.125f);
	
	u32   fluvial_iteration_count  = 16;
	float fluvial_inertia          = 0.98f;
	float fluvial_viscosity        = 0.02f;
	float fluvial_erosion_rate     = 0.05f;
	float fluvial_deposition_rate  = 0.1f;
	float fluvial_evaporation_rate = 0.02f;
	float fluvial_initial_water    = 1.f;
};

NOTES(Meta::HlslFile{ terrain_editor_data_filename })
struct TerrainHeightLayerErosionGpuSettings {
	u32   random_seed = 0;
	u32   iteration_index          = 0;
	u32   iteration_count          = 0;
	float fluvial_inertia          = 0.f;
	float fluvial_viscosity        = 0.f;
	float fluvial_erosion_rate     = 0.f;
	float fluvial_deposition_rate  = 0.f;
	float fluvial_evaporation_rate = 0.f;
	float fluvial_initial_water    = 0.f;
};

NOTES(Meta::EntityType{ 16 }, Meta::ComponentQuery{})
struct TerrainHeightLayerErosionEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	ECS::Component<HierarchyComponent> hierarchy;
	
	ECS::Component<TerrainHeightLayerErosionCpuSettings> settings;
};

NOTES(Meta::SaveLoadOptions{ SaveLoadFlags::None })
struct TerrainEditorLayerStackBuildState {
	u64 hash                = 0;
	u64 end_command_index   = 0;
	u32 height_field_epoch  = 0;
	u32 min_ready_mip_level = 0;
	
	s32 min_frequency_band = TerrainEditorEqualizer::min_frequency_band;
};

NOTES(Meta::EntityType{ 4 }, Meta::ComponentQuery{})
struct TerrainEditorLayerStackEntityType {
	ECS::Component<GuidComponent> guid;
	ECS::Component<NameComponent> name;
	
	ECS::Component<HierarchyComponent> hierarchy;
	ECS::Component<TerrainEditorLayerStackBuildState> build_state;
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
	TerrainHeightLayerErosionCpuSettings*    erosion_cpu_settings    = nullptr;
};
