#ifndef MESHLETVERTEXDECODING_HLSL
#define MESHLETVERTEXDECODING_HLSL

struct MeshletBufferOffsets {
	uint vertex_buffer_offset;
	uint index_buffer_offset;
};

MeshletBufferOffsets ComputeMeshletBufferOffsets(MeshletHeader meshlet, u32 meshlet_header_offset) {
	MeshletBufferOffsets offsets;
	offsets.vertex_buffer_offset = meshlet_header_offset + sizeof(MeshletHeader);
	offsets.index_buffer_offset  = offsets.vertex_buffer_offset + meshlet.vertex_count * sizeof(MeshletVertex);
	
	return offsets;
}

MeshletVertex LoadMeshletVertexBuffer(ByteAddressBuffer mesh_asset_buffer, u32 vertex_buffer_offset, u32 vertex_index) {
	return mesh_asset_buffer.Load<MeshletVertex>(vertex_buffer_offset + vertex_index * sizeof(MeshletVertex));
}

uint3 LoadMeshletIndexBuffer(ByteAddressBuffer mesh_asset_buffer, u32 index_buffer_offset, u32 triangle_index) {
	compile_const u32 triangle_size_bytes = 3;
	
	u32   load_offset    = index_buffer_offset + triangle_index * triangle_size_bytes;
	u16x2 packed_indices = mesh_asset_buffer.Load<u16x2>(load_offset & ~0x1);
	
	u32 indices = ((u32)packed_indices.x | (u32)packed_indices.y << 16) >> ((load_offset & 0x1) * 8);
	return uint3(indices >> 0, indices >> 8, indices >> 16) & 0xFF;
}

float3 DecodeMeshletVertexPosition(u16x3 position, GpuMeshAssetData mesh_asset, MeshletHeader meshlet) {
	return float3(position) * mesh_asset.rcp_quantization_scale + meshlet.position_offset;
}

float3 DecodeMeshletVertexPosition(float3 position, GpuMeshAssetData mesh_asset, MeshletHeader meshlet) {
	return position;
}

// TODO: Pass in correct bitangent sign.
float3x3 ComputeTangentToOtherSpace(float3 other_space_tangent, float3 other_space_normal, float bitangent_sign = -1.0) {
	return transpose(float3x3(other_space_tangent, bitangent_sign * cross(other_space_normal, other_space_tangent), other_space_normal));
}

template<typename T>
T BarycentricInterpolation(float3 barycentrics, T v0, T v1, T v2) {
	return v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
}

float3 DecodeAndInterpolateUnitVector(float3 barycentrics, s16x2 v0, s16x2 v1, s16x2 v2) {
	float3 n0 = DecodeOctahedralMap(DecodeR16G16_SNORM(v0));
	float3 n1 = DecodeOctahedralMap(DecodeR16G16_SNORM(v1));
	float3 n2 = DecodeOctahedralMap(DecodeR16G16_SNORM(v2));
	return BarycentricInterpolation(barycentrics, n0, n1, n2);
}

struct BarycentricsWithDerivatives {
	float3 barycentrics;
	float3 barycentrics_ddx;
	float3 barycentrics_ddy;
};

// Based on SIGGRAPH 2024 "Variable Rate Shading with Visibility Buffer Rendering" by John Hable.
// Code by James McLaren and Stephen Hill. (public domain)
BarycentricsWithDerivatives ComputeBarycentricsWithDerivatives(float4 v0, float4 v1, float4 v2, float2 pixel_ndc, float2 render_target_size) {
	BarycentricsWithDerivatives result = (BarycentricsWithDerivatives)0;
	
	float3 inv_w = rcp(float3(v0.w, v1.w, v2.w));
	
	float2 ndc0 = v0.xy * inv_w.x;
	float2 ndc1 = v1.xy * inv_w.y;
	float2 ndc2 = v2.xy * inv_w.z;
	
	float inv_det = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	result.barycentrics_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * inv_det * inv_w;
	result.barycentrics_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * inv_det * inv_w;
	float ddx_sum = dot(result.barycentrics_ddx, float3(1.0, 1.0, 1.0));
	float ddy_sum = dot(result.barycentrics_ddy, float3(1.0, 1.0, 1.0));
	
	float2 delta_vec = pixel_ndc - ndc0;
	float interp_inv_w = inv_w.x + delta_vec.x * ddx_sum + delta_vec.y * ddy_sum;
	float interp_w = rcp(interp_inv_w);
	
	result.barycentrics.x = interp_w * (delta_vec.x * result.barycentrics_ddx.x + delta_vec.y * result.barycentrics_ddy.x + inv_w.x);
	result.barycentrics.y = interp_w * (delta_vec.x * result.barycentrics_ddx.y + delta_vec.y * result.barycentrics_ddy.y);
	result.barycentrics.z = interp_w * (delta_vec.x * result.barycentrics_ddx.z + delta_vec.y * result.barycentrics_ddy.z);
	
	result.barycentrics_ddx *= (2.0 / render_target_size.x);
	result.barycentrics_ddy *= (2.0 / render_target_size.y);
	ddx_sum *= (2.0 / render_target_size.x);
	ddy_sum *= (2.0 / render_target_size.y);
	
	result.barycentrics_ddy *= -1.0;
	ddy_sum                 *= -1.0;
	
	float interp_w_ddx = 1.0 / (interp_inv_w + ddx_sum);
	float interp_w_ddy = 1.0 / (interp_inv_w + ddy_sum);
	
	result.barycentrics_ddx = interp_w_ddx * (result.barycentrics * interp_inv_w + result.barycentrics_ddx) - result.barycentrics;
	result.barycentrics_ddy = interp_w_ddy * (result.barycentrics * interp_inv_w + result.barycentrics_ddy) - result.barycentrics;  
	
	return result;
}

#endif // MESHLETVERTEXDECODING_HLSL
