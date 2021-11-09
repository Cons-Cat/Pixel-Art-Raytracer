// (Descriptor Binding, Descriptor Set)
[vk::binding(0, 0)]
RWTexture2D<float4> uav;

[vk::binding(1, 0)]
RWStructuredBuffer<Cell> pixel_buffer_cells;

struct Pixel {
    uint32_t palette_index;
    uint32_t depth;
}

struct Cell {
    Pixel pixels[4 * 4 * 8];
}

struct GpuPixelBuffer {
    Cell cells[120 * 75];
}

[shader("compute")]
[numthreads(4, 4, 4)]
void compute_main(uint3 group_id : SV_GroupID,
                  uint3 group_thread_id : SV_GroupThreadID,
                  uint3 dispatch_thread_id : SV_DispatchThreadID,
                  uint group_index : SV_GroupIndex) {
    uint min_depth = 255;
    uint current_cell = group_id.x + group_id.y * 75;
    uint nearest_pixel_index = 0;
    uint thread_x = dispatch_thread_id.x;
    uint thread_y = dispatch_thread_id.y;
    uint thread_z = dispatch_thread_id.z;
    bool thread_is_even = dispatch_thread_id.z % 2 == 0;

    // The last element should never try swapping with an element ahead of it.
    if (thread_z < 7) {
        // TODO: I don't think 8 is guaranteed to be enough iterations.
        for (uint i = 0; i < 8; i++) {
            uint current_index = (thread_x + thread_y * 4) * 8 + thread_z;
            // TODO: There exist atomic-compare-exchange instructions that might
            // be more efficient.

            // Odd sort:
            if (!thread_is_even) {
                Pixel temp_pixel =
                    pixel_buffer_cells[current_cell].pixels[current_index];
                if (pixel_buffer_cells[current_cell]
                        .pixels[current_index + 1]
                        .depth < temp_pixel.depth) {
                    pixel_buffer_cells[current_cell].pixels[current_index] =
                        pixel_buffer_cells[current_cell]
                            .pixels[current_index + 1];
                    pixel_buffer_cells[current_cell].pixels[current_index + 1] =
                        temp_pixel;
                }
            }
            GroupMemoryBarrierWithGroupSync();

            // Even sort:
            if (thread_is_even) {
                Pixel temp_pixel =
                    pixel_buffer_cells[current_cell].pixels[current_index];
                if (pixel_buffer_cells[current_cell]
                        .pixels[current_index + 1]
                        .depth < temp_pixel.depth) {
                    pixel_buffer_cells[current_cell].pixels[current_index] =
                        pixel_buffer_cells[current_cell]
                            .pixels[current_index + 1];
                    pixel_buffer_cells[current_cell].pixels[current_index + 1] =
                        temp_pixel;
                }
            }
            
            GroupMemoryBarrierWithGroupSync();
        }
    }

    // The first element is the lowest depth after sorting.
    Pixel nearest_pixel = pixel_buffer_cells[current_cell].pixels[0];

    if (thread_z == 0) {
        if (nearest_pixel.palette_index == 0) {
            uav[dispatch_thread_id.xy] = float4(0.9, 1, 0, 1);
        } else if (nearest_pixel.palette_index == 1) {
            uav[dispatch_thread_id.xy] = float4(0.65f, 0, 0, 1);
        } else if (nearest_pixel.palette_index == 2) {
            uav[dispatch_thread_id.xy] = float4(0.3f, 0, 1, 1);
        } else {
            uav[dispatch_thread_id.xy] = float4(0, 0, 0, 1);
        }
    }
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
    float4 sv_position = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);
    return VertexStageOutput(sv_position);
}

[shader("fragment")] float4
fragment_main(VertexStageOutput vertex,
              float4 pixel_coord : SV_Position) : SV_Target {
    float4 color;
    color = uav.Load(pixel_coord.xy);
    return color;
}
