#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float4 color    : COLOR;
	float2 texcoord : TEXCOORD;
};

#if defined(VERTEX_SHADER)
InputPS MainVS(uint vertex_id : SV_VertexID) {
	float4 positions[3] = {
		float4(+0.0, -0.5, 0.5, 1.0),
		float4(+0.5, +0.5, 0.5, 1.0),
		float4(-0.5, +0.5, 0.5, 1.0),
	};
	
	InputPS output;
	output.position = positions[vertex_id];
	output.color    = constants.view_to_clip_coef;
	output.texcoord = 0.0;
	
	return output;
}
#endif // defined(VERTEX_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input) : SV_Target0  {
	return input.color;
}
#endif // defined(PIXEL_SHADER)
