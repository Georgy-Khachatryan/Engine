#ifndef DEBUGDRAWLIST_HLSL
#define DEBUGDRAWLIST_HLSL

#include "Basic.hlsl"
#include "Generated/DebugGeometryData.hlsl"

// Based on the ImGuiDrawList3D, keep this code in sync with it.

void AddInstanceOfType(DebugMeshInstanceType instance_type, float3 position, u32 color, quat rotation, float4 packed_data) {
	uint instance_offset = 0;
	InterlockedAdd(debug_geometry_indirect_arguments[instance_type].instance_count, 1u, instance_offset);
	if (instance_offset >= DebugGeometrySettings::debug_mesh_instance_count) {
		InterlockedAdd(debug_geometry_indirect_arguments[instance_type].instance_count, -1u);
		return;
	}
	
	DebugMeshInstance instance;
	instance.position    = position;
	instance.color       = color;
	instance.rotation    = EncodeR16G16B16A16_SNORM(rotation);
	instance.packed_data = (float16x4)packed_data;
	
	uint instance_index = instance_offset + DebugGeometrySettings::debug_mesh_instance_count * instance_type;
	debug_mesh_instances[instance_index] = instance;
}

void AddSphere(float3 position, float radius, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Sphere, position, color, quat(0.0, 0.0, 0.0, 1.0), float4(radius, radius, radius, 0.0));
}

void AddSphere(float3 position, quat rotation, float3 radius, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Sphere, position, color, rotation, float4(radius, 0.0));
}

void AddCube(float3 position, quat rotation, float3 half_extent, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Cube, position, color, rotation, float4(half_extent, 0.0));
}

void AddCylinder(float3 position, quat rotation, float height, float radius_0, float radius_1, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Cylinder, position, color, rotation, float4(height, radius_0, radius_1, 0.0));
}

void AddTorus(float3 position, quat rotation, float major_radius, float minor_radius, u32 color) {
	AddInstanceOfType(DebugMeshInstanceType::Torus, position, color, rotation, float4(major_radius, minor_radius, 0.0, 0.0));
}

void AddArrow(float3 position, float3 direction, float length, float radius, u32 color) {
	quat rotation = Conjugate(AxisAxisZToQuat(direction));
	
	float arrow_head_radius  = radius * 2.0;
	float arrow_head_length  = arrow_head_radius * 3.0; // 3:1 arrow_head_length to arrow_head_diameter ratio.
	float arrow_shaft_length = max(length - arrow_head_length, 0.0);
	
	AddCylinder(position, rotation, arrow_shaft_length, radius, radius, color);
	AddCylinder(position + direction * arrow_shaft_length, rotation, arrow_head_length, arrow_head_radius, 0.0, color);
}

void AddArrow(float3 from, float3 to, float radius, u32 color) {
	float3 direction = (to - from);
	float direction_length = length(direction);
	if (direction_length == 0.0) return;
	
	AddArrow(from, direction * (1.0 / direction_length), direction_length, radius, color);
}

bool IsHoveredPixel(uint2 pixel_coordinates) {
	return all((s32x2)pixel_coordinates == scene.mouse_cursor_position);
}

#endif // DEBUGDRAWLIST_HLSL
