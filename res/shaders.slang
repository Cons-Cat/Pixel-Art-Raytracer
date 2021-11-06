[shader("compute")]
void compute_main() {
}

// Output from vertex shader.
struct VertexStageOutput {
    float4 sv_position : SV_Position;
	 __init(float4 position)
    {
        sv_position = position;
    }
};

[shader("vertex")]
VertexStageOutput vertex_main(uint vertex_id : SV_VertexID) {
	// Transfom the first three verts into a triangle.
	uint2 out_uv = float2((vertex_id << 1) & 2, vertex_id & 2);
	float4 sv_position = float4(out_uv * 2.0 - 1.0, 0.0, 1.0) ;
    return VertexStageOutput(sv_position);
}

[[vk::binding(0)]]
RWTexture2D<float4> uav;

[shader("fragment")]
float4 fragment_main(VertexStageOutput vertex, float4 pixel_coord : SV_Position) : SV_Target {
	return uav.Load(pixel_coord.xy);
}
