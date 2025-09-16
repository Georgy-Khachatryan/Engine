

struct InputPS {
	float4 position : SV_Position;
	float3 color    : COLOR;
};

#if defined(VERTEX_SHADER)
InputPS MainVS(uint vertex_id : SV_VertexID) {
	float4 positions[3] = {
		float4(+0.0, -0.5, 0.5, 1.0),
		float4(+0.5, +0.5, 0.5, 1.0),
		float4(-0.5, +0.5, 0.5, 1.0),
	};
	
	float3 colors[3] = {
		float3(1.0, 0.0, 0.0),
		float3(0.0, 1.0, 0.0),
		float3(0.0, 0.0, 1.0),
	};
	
	InputPS output;
	output.position = positions[vertex_id];
	output.color    = colors[vertex_id];
	
#if defined(RED_COLOR)
	output.color = float3(1.0, 0.0, 0.0);
#endif // defined(RED_COLOR)
	
#if defined(BLUE_COLOR)
	output.color = float3(0.0, 0.0, 1.0);
#endif // defined(BLUE_COLOR)
	
	return output;
}
#endif // defined(VERTEX_SHADER)


struct OutputPS {
	float4 color : SV_Target0;
};

#if defined(PIXEL_SHADER)
OutputPS MainPS(InputPS input) {
	OutputPS output;
	output.color = float4(input.color, 1.0);
	
	return output;
}
#endif // defined(PIXEL_SHADER)

