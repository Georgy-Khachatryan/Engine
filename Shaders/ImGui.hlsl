#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float4 color    : COLOR;
	float2 texcoord : TEXCOORD;
};

float4 DecodeR8G8B8A8(uint encoded) {
	return float4(uint4(encoded >> 0, encoded >> 8, encoded >> 16, encoded >> 24) & 0xFF) * (1.0 / 255.0);
}

#if defined(VERTEX_SHADER)
InputPS MainVS(uint start_vertex_location : SV_StartVertexLocation, uint vertex_id : SV_VertexID) {
	ImGuiVertex vertex = vertices[start_vertex_location + vertex_id];
	
	InputPS output;
	output.position.x = constants.view_to_clip_coef.x * vertex.position.x + constants.view_to_clip_coef.z;
	output.position.y = constants.view_to_clip_coef.y * vertex.position.y + constants.view_to_clip_coef.w;
	output.position.z = 0.5;
	output.position.w = 1.0;
	output.color      = DecodeR8G8B8A8(vertex.color);
	output.texcoord   = vertex.texcoord;
	
	return output;
}
#endif // defined(VERTEX_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input) : SV_Target0  {
	return input.color * font_texture.Sample(sampler_linear_clamp, input.texcoord);
}
#endif // defined(PIXEL_SHADER)
