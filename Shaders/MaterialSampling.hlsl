#ifndef MATERIALSAMPLING_HLSL
#define MATERIALSAMPLING_HLSL
#include "Basic.hlsl"

struct MaterialProperties {
	float16x3 albedo;
	float16x3 normal;
	float16 roughness;
	float16 metalness;
};

struct TexcoordStream {
	float2 texcoord;
#if !defined(PIXEL_SHADER)
	float2 texcoord_ddx;
	float2 texcoord_ddy;
#endif // !defined(PIXEL_SHADER)
};

template<typename T>
T SampleMaterialTexture(u32 texture_index, TexcoordStream texcoord_stream, T default_value) {
	if (texture_index & MaterialTextureIndexFlags::UseDefault) return default_value;
	
#if defined(PIXEL_SHADER)
	Texture2D<T> texture = ResourceDescriptorHeap[texture_index];
	return texture.Sample(sampler_linear_wrap, texcoord_stream.texcoord);
#else // !defined(PIXEL_SHADER)
	Texture2D<T> texture = ResourceDescriptorHeap[NonUniformResourceIndex(texture_index)];
	return texture.SampleGrad(sampler_linear_wrap, texcoord_stream.texcoord, texcoord_stream.texcoord_ddx, texcoord_stream.texcoord_ddy);
#endif // !defined(PIXEL_SHADER)
}

MaterialProperties SampleMaterial(u32 material_index, TexcoordStream texcoord_stream) {
	MaterialProperties result;
	
	if (material_index != u32_max) {
		GpuMaterialTextureData material = material_texture_data[material_index];
		result.albedo = SampleMaterialTexture(material.albedo, texcoord_stream, DecodeR10G10B10<float16>(material.albedo));
		result.normal = DecodeHemiOctahedralMap01(SampleMaterialTexture(material.normal, texcoord_stream, float16x2(0.5, 0.5)));
		result.roughness = SampleMaterialTexture(material.roughness, texcoord_stream, asfloat16((u16)material.roughness));
		result.metalness = SampleMaterialTexture(material.metalness, texcoord_stream, asfloat16((u16)material.metalness));
	} else {
		result.albedo = float16x3(0.5, 0.5, 0.5);
		result.normal = float16x3(0.0, 0.0, 1.0);
		result.roughness = 0.5;
		result.metalness = 0.0;
	}
	
	return result;
}

#endif // MATERIALSAMPLING_HLSL
