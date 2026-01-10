#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float3 normal : NORMAL;
};

#if defined(VERTEX_SHADER)
float3 SphereVertexToModelSpace(float4 vertex, float3 radius) {
	return vertex.xyz * radius;
}

float3 CubeVertexToModelSpace(float4 vertex, float3 half_extent) {
	return vertex.xyz * half_extent;
}

float3 CylinderVertexToModelSpace(float4 vertex, float half_height, float2 radius) {
	return float3(vertex.xy * lerp(radius.x, radius.y, vertex.z * 0.5 + 0.5), vertex.z * half_height);
}

float3 TorusVertexToModelSpace(float4 vertex, float major_radius, float minor_radius) {
	return float3(vertex.xy * (major_radius - vertex.z * minor_radius), vertex.w * minor_radius);
}

InputPS MainVS(uint vertex_id : SV_VertexID) {
	float4 parametric_vertex = vertices[vertex_id];
	
	// float3 vertex = SphereVertexToModelSpace(parametric_vertex, 0.5);
	// float3 vertex = CubeVertexToModelSpace(parametric_vertex, float3(1.0, 0.5, 0.125));
	// float3 vertex = CylinderVertexToModelSpace(parametric_vertex, 2.0, float2(2.0, 0.125));
	float3 vertex = TorusVertexToModelSpace(parametric_vertex, 2.0, 0.5);
	
	float3 position = float3(0.0, 0.0, 4.0);
	float4 rotation = float4(0.5, 0.5, 0.5, 0.5);
	
	float3 world_space_position = QuatMul(rotation, vertex) + position;
	float3 view_space_position  = mul(scene.world_to_view, float4(world_space_position, 1.0));
	
	InputPS output;
	output.position = TransformViewToClipSpace(view_space_position, scene.view_to_clip_coef);
	output.normal   = normalize(vertex.xyz);
	
	return output;
}
#endif // defined(VERTEX_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input, float3 bary : SV_Barycentrics) : SV_Target0  {
	float wireframe = BarycentricWireframe(bary, ddx(bary), ddy(bary));
	return float4(wireframe * (input.normal * 0.5 + 0.5), 1.0);
}
#endif // defined(PIXEL_SHADER)
