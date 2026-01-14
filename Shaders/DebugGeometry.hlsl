#include "Basic.hlsl"

struct InputPS {
	float4 position : SV_Position;
	float4 color  : COLOR;
	float3 normal : NORMAL;
};

#if defined(VERTEX_SHADER)
float3 SphereVertexToModelSpace(float4 vertex, float3 radius) {
	return vertex.xyz * radius;
}

float3 CubeVertexToModelSpace(float4 vertex, float3 half_extent) {
	return vertex.xyz * half_extent;
}

float3 CylinderVertexToModelSpace(float4 vertex, float height, float2 radius) {
	return float3(vertex.xy * lerp(radius.x, radius.y, vertex.z), vertex.z * height);
}

float3 TorusVertexToModelSpace(float4 vertex, float major_radius, float minor_radius) {
	return float3(vertex.xy * (major_radius - vertex.z * minor_radius), vertex.w * minor_radius);
}

InputPS MainVS(uint start_vertex_location : SV_StartVertexLocation, uint vertex_id : SV_VertexID, uint start_instance_location : SV_StartInstanceLocation, uint instance_id : SV_InstanceID) {
	DebugMeshInstance instance = instances[start_instance_location + instance_id];
	float4 parametric_vertex = vertices[start_vertex_location + vertex_id];
	
	float4 instance_data = DecodeR16G16B16A16_FLOAT(instance.packed_data);
	
	float3 vertex = 0.0;
	switch (constants.instance_type) {
	case DebugMeshInstanceType::Sphere:   vertex = SphereVertexToModelSpace(parametric_vertex,   instance_data.xyz); break;
	case DebugMeshInstanceType::Cube:     vertex = CubeVertexToModelSpace(parametric_vertex,     instance_data.xyz); break;
	case DebugMeshInstanceType::Cylinder: vertex = CylinderVertexToModelSpace(parametric_vertex, instance_data.x, instance_data.yz); break;
	case DebugMeshInstanceType::Torus:    vertex = TorusVertexToModelSpace(parametric_vertex,    instance_data.x, instance_data.y);  break;
	default: break;
	}
	
	float3 world_space_position = QuatMul(DecodeR16G16B16A16_SNORM(instance.rotation), vertex) + instance.position;
	float3 view_space_position  = mul(scene.world_to_view, float4(world_space_position, 1.0));
	
	InputPS output;
	output.position = TransformViewToClipSpace(view_space_position, scene.view_to_clip_coef);
	output.color    = DecodeR8G8B8A8_UNORM_SRGB(instance.color);
	output.normal   = all(vertex == 0.0) ? 0.0 : normalize(vertex);
	
	return output;
}
#endif // defined(VERTEX_SHADER)

#if defined(PIXEL_SHADER)
float4 MainPS(InputPS input, float3 bary : SV_Barycentrics) : SV_Target0  {
	return float4(lerp(input.normal * 0.5 + 0.5, input.color.xyz, input.color.w), 1.0);
}
#endif // defined(PIXEL_SHADER)
