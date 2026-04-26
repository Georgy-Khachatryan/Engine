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

// TODO: Pass in correct bitangent sign.
float3x3 ComputeTangentToOtherSpace(float3 other_space_tangent, float3 other_space_normal, float bitangent_sign = -1.0) {
	return transpose(float3x3(other_space_tangent, bitangent_sign * cross(other_space_normal, other_space_tangent), other_space_normal));
}

#endif // MESHLETVERTEXDECODING_HLSL
