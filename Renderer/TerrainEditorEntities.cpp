#include "TerrainEditorEntities.h"

TerrainEditorEqualizer TerrainEditorEqualizer::MakePreset(TerrainEditorEqualizerPreset preset, float scale) {
	TerrainEditorEqualizer result;
	result.scale = scale;
	
	compile_const s32 min_frequency_band = TerrainEditorEqualizer::min_frequency_band;
	compile_const s32 max_frequency_band = min_frequency_band + 6; // Limit to the first 7 bands.
	compile_const float inv_frequency_band_range = 1.f / (float)(max_frequency_band - min_frequency_band);
	static_assert(max_frequency_band <= TerrainEditorEqualizer::max_frequency_band);
	
	for (s32 i = min_frequency_band; i <= max_frequency_band; i += 1) {
		float t = (float)(i - min_frequency_band) * inv_frequency_band_range;
		
		switch (preset) {
		case TerrainEditorEqualizerPreset::None:           result[i] = 0.f;                                         break;
		case TerrainEditorEqualizerPreset::Neutral:        result[i] = 1.f;                                         break;
		case TerrainEditorEqualizerPreset::LowPass:        result[i] = Math::Pow2(sinf(t * Math::HALF_PI));         break;
		case TerrainEditorEqualizerPreset::HighPass:       result[i] = Math::Pow2(sinf((t + 1.f) * Math::HALF_PI)); break;
		case TerrainEditorEqualizerPreset::Linear:         result[i] = t;                                           break;
		case TerrainEditorEqualizerPreset::InverseLinear:  result[i] = 1.f - t;                                     break;
		case TerrainEditorEqualizerPreset::VShaped:        result[i] = fabsf(t - 0.5f) * 2.f;                       break;
		case TerrainEditorEqualizerPreset::InverseVShaped: result[i] = 1.f - fabsf(t - 0.5f) * 2.f;                 break;
		case TerrainEditorEqualizerPreset::UShaped:        result[i] = Math::Pow2(fabsf(t - 0.5f) * 2.f);           break;
		case TerrainEditorEqualizerPreset::InverseUShaped: result[i] = 1.f - Math::Pow2(fabsf(t - 0.5f) * 2.f);     break;
		}
	}
	
	return result;
}
