#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float4 color    : COLOR;
	float2 texcoord : TEXCOORD;
};

#if defined(VERTEX_SHADER)
InputPS MainVS(uint start_vertex_location : SV_StartVertexLocation, uint vertex_id : SV_VertexID) {
	ImGuiVertex vertex = vertices[start_vertex_location + vertex_id];
	
	InputPS output;
	output.position.x = constants.view_to_clip_coef.x * vertex.position.x + constants.view_to_clip_coef.z;
	output.position.y = constants.view_to_clip_coef.y * vertex.position.y + constants.view_to_clip_coef.w;
	output.position.z = 0.5;
	output.position.w = 1.0;
	output.color      = DecodeR8G8B8A8_UNORM_SRGB(vertex.color); // Interpolate colors in linear space.
	output.texcoord   = vertex.texcoord;
	
	return output;
}
#endif // defined(VERTEX_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input) : SV_Target0  {
	Texture2D<float4> texture = ResourceDescriptorHeap[texture_id.index];
	return input.color * texture.Sample(sampler_linear_clamp, input.texcoord);
}
#endif // defined(PIXEL_SHADER)
