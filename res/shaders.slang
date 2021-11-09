// (Descriptor Binding, Descriptor Set)
[vk::binding(0, 0)]
RWTexture2D<float4> uav;

[vk::binding(1, 0)]
RWStructuredBuffer<Cell> pixel_buffer_cells;

struct Pixel {
    uint32_t palette_index;
    uint32_t depth;
    // uint16_t empty_data; // Padding.

    uint32_t screen_x;
    uint32_t screen_y;
    
    // float3 normals;
}

struct Cell {
    Pixel pixels[8 * 8 * 8];
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
    if (dispatch_thread_id.x > 480 || dispatch_thread_id.y > 300) {
       // return;
    }
    uint min_depth = 255;
    uint current_cell = group_id.x + group_id.y * 60;
    uint nearest_pixel_index;

    // Iterate through all pixels in the cell.
    for (uint i = 0; i < 8 * 8 * 8; i++) {
        Pixel current_pixel = pixel_buffer_cells[current_cell].pixels[i];
        // 255 is a value that cannot be used safely anyways, so it is a sentinel.
        if (current_pixel.depth == 255) {
            // break;
        }
        // if (current_pixel.screen_x
        //     == dispatch_thread_id.x
        //  && current_pixel.screen_y
        //     == dispatch_thread_id.y) {
            if (current_pixel.depth < min_depth) {
                min_depth = current_pixel.depth;
                nearest_pixel_index = i;
            }
        // }
    }

    Pixel nearest_pixel = pixel_buffer_cells[current_cell]
        .pixels[nearest_pixel_index];

    if (nearest_pixel.palette_index == 0) {
       uav[dispatch_thread_id.xy] = float4(0.9,1,0,1);
    } else if (nearest_pixel.palette_index == 1) {
       uav[dispatch_thread_id.xy] = float4(0.65f,0,0,1);
    } else if (nearest_pixel.palette_index == 2) {
       uav[dispatch_thread_id.xy] = float4(0.3f,0,1,1);
    } else {
       uav[dispatch_thread_id.xy] = float4(0,0,0,1);
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
