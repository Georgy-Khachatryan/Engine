#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD;
};

#if defined(VERTEX_SHADER)
InputPS MainVS(uint vertex_id : SV_VertexID) {
	BasicVertex vertex = vertices[vertex_id];
	
	float3 view_space_position = mul(scene.world_to_view, float4(vertex.position, 1.0)) + float3(0.0, 0.0, 10.0);
	
	InputPS output;
	output.position = TransformViewToClipSpace(view_space_position, scene.view_to_clip_coef);
	output.texcoord = vertex.texcoord;
	
	return output;
}
#endif // defined(VERTEX_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input) : SV_Target0 {
	return float4(input.texcoord, 0.0, 1.0);
}
#endif // defined(PIXEL_SHADER)
