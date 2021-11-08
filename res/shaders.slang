// (Descriptor Binding, Descriptor Set)
[vk::binding(0, 0)]
RWTexture2D<float4> uav;

[vk::binding(1, 0)]
RWStructuredBuffer<GpuPixelBuffer> pixel_buffer_data;

struct Pixel {
  uint8_t palette_index;
  uint16_t normal_vector; // Three nibbles.
  uint8_t depth;
}

struct Cell {
    Pixel pixels[8*8];
}

struct GpuPixelBuffer {
    Cell cells[38 * 60];
}

[shader("compute")]
[numthreads(8,8,1)]
void compute_main(
    uint3 group_id : SV_GroupID,
    uint3 group_thread_id : SV_GroupThreadID,
    uint3 dispatch_thread_id : SV_DispatchThreadID,
    uint group_index : SV_GroupIndex) {
	float even = ((group_id.x + group_id.y) % 2) + 1;
    // Store a color in the storage image.
	uav[dispatch_thread_id.xy] = float4(
        float(dispatch_thread_id.x)/480*even,
	    float(dispatch_thread_id.y)/300*even,
	    pixel_buffer_data[0].cells[0].pixels[0].palette_index, // TODO: Delete this test.
	    1);
}

// Output from vertex shader.
struct VertexStageOutput {
    float4 sv_position : SV_Position;
     __init(float4 position) {
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

[shader("fragment")]
float4 fragment_main(VertexStageOutput vertex, float4 pixel_coord : SV_Position) : SV_Target {
    float4 color;
    // float4 color = float4(1,1,1,1);
    color = uav.Load(pixel_coord.xy);
    return color;
}
