import palette;

// (Descriptor Binding, Descriptor Set)
[vk::binding(0, 0)]
RWTexture2D<float4> uav;

// TODO: Should this be a cbuffer?
[vk::binding(1, 0)]
RWStructuredBuffer<GpuPixelBuffer> gpu_buffer;

// TODO: Assess performance characteristics
//       https://developer.nvidia.com/content/understanding-structured-buffer-performance
struct Pixel {
    float3 normal;
	float alpha;
    int32_t depth;
	uint32_t palette_index;
};

struct PixelBucket {
    int32_t size;
    Pixel pixels[8];
};

struct Cell {
    PixelBucket pixel_buckets[4 * 4];
};

struct PointLight {
    int3 position;
};

static const int point_light_count = 1;

struct GpuPixelBuffer {
    Cell cells[75 * 120];
	PointLight point_lights[point_light_count];
};

// Thread ids are inherently unsigned.
void sort_pixel_bucket(uint32_t thread_x, uint32_t thread_y, uint32_t thread_z,
                       uint32_t thread_bucket_index, uint32_t current_cell,
					   bool thread_is_even) {
    uint32_t thread_pixel_index = thread_z * 2;
	
    // TODO: Dissassemble -O2 to see if this check should be at the function's call site.
    // The last element should never try swapping with an element ahead of it.
    if (thread_z < 3) {
        // Parallel odd-even sort has O(N) time complexity, so we iterate through
        // the size of a bucket, which is 8.
        for (int32_t i = 0; i < 8; i++) {
            // TODO: There exist atomic-compare-exchange instructions that might
            // be more efficient than this algorithm.
            uint32_t odd_sort = (i + 1) % 2;

            // Odd sort:
            Pixel temp_pixel = gpu_buffer[0].cells[current_cell]
            .pixel_buckets[thread_bucket_index]
            .pixels[thread_pixel_index + odd_sort];
                    
            if (gpu_buffer[0].cells[current_cell]
            .pixel_buckets[thread_bucket_index]
            .pixels[thread_pixel_index + odd_sort + 1]
            .depth < temp_pixel.depth) {
                gpu_buffer[0].cells[current_cell]
                .pixel_buckets[thread_bucket_index]
                .pixels[thread_pixel_index + odd_sort] =
                        gpu_buffer[0].cells[current_cell]
                        .pixel_buckets[thread_bucket_index]
                        .pixels[thread_pixel_index + odd_sort + 1];
                    
                gpu_buffer[0].cells[current_cell]
                .pixel_buckets[thread_bucket_index]
                .pixels[thread_pixel_index + odd_sort + 1] = temp_pixel;
            }

            // Waiting is only necessary when sorting across groups.
            if (odd_sort == 1) {
               GroupMemoryBarrierWithGroupSync();
            }
        }
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void color_main(uint3 group_id : SV_GroupID,
                  uint3 group_thread_id : SV_GroupThreadID,
                  uint3 dispatch_thread_id : SV_DispatchThreadID,
                  uint group_index : SV_GroupIndex) {
    uint32_t thread_x = group_thread_id.x; // Bounded in [0, 3] 
    uint32_t thread_y = group_thread_id.y; // Bounded in [0, 3]
    uint32_t thread_z = group_thread_id.z; // Bounded in [0, 3]
    uint32_t thread_bucket_index = thread_x + thread_y * 4;
    uint32_t current_cell = group_id.x + group_id.y * (480 / 4);
    bool thread_is_even = dispatch_thread_id.z % 2 == 0;

    if (gpu_buffer[0].cells[current_cell].pixel_buckets[thread_bucket_index].pixels[0].palette_index == 0){
	        uav[dispatch_thread_id.xy] = float4(0, 0, 0, 1.0f);
	return;}
	
    sort_pixel_bucket(thread_x, thread_y, thread_z,
                      thread_bucket_index, current_cell,
					  thread_is_even);
 
    if (thread_z == 0) {
        // The first element is the lowest depth after sorting.
        Pixel nearest_pixel = gpu_buffer[0].cells[current_cell]
            .pixel_buckets[thread_bucket_index]
            .pixels[0];

        Lab color = color_palette[(int)nearest_pixel.palette_index];
		// TODO: Bruh.

        float3 position = float3(
		    dispatch_thread_id.x,
			dispatch_thread_id.y + float3(0, 0.5f, 0).y * nearest_pixel.depth, 
			dispatch_thread_id.z + float3(0, 0, 0.5f).z * nearest_pixel.depth, 
        );
		// 0.5f ambient lighting.
		float brightness = 0.0f;
        for(int i = 0; i < point_light_count; i++) {
		    // Calculate reflection of this light.
			// TODO: Optimize point_lights int3 to float3 cast.
			float3 light_vector =
			    normalize(gpu_buffer[0].point_lights[i].position - float3(position));
			brightness = max(dot(light_vector, nearest_pixel.normal), 0.65f);
        }

        color.L *= brightness;
		color.a *= brightness;
		color.b *= brightness;
        float3 rgb = oklab_to_linear_srgb(color);
        uav[dispatch_thread_id.xy] = float4(rgb, 1.0f);
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void depth_main(uint3 group_id : SV_GroupID,
                  uint3 group_thread_id : SV_GroupThreadID,
                  uint3 dispatch_thread_id : SV_DispatchThreadID,
                  uint group_index : SV_GroupIndex) {
    uint32_t thread_x = group_thread_id.x; // Bounded in [0, 3] 
    uint32_t thread_y = group_thread_id.y; // Bounded in [0, 3]
    uint32_t thread_z = group_thread_id.z; // Bounded in [0, 3]
    uint32_t thread_bucket_index = thread_x + thread_y * 4;
    uint32_t current_cell = group_id.x + group_id.y * (480 / 4);
    bool thread_is_even = dispatch_thread_id.z % 2 == 0;

    sort_pixel_bucket(thread_x, thread_y, thread_z,
                      thread_bucket_index, current_cell,
					  thread_is_even);
 
    if (thread_z == 0) {
        // The first element is the lowest depth after sorting.
        Pixel nearest_pixel = gpu_buffer[0].cells[current_cell]
            .pixel_buckets[thread_bucket_index]
            .pixels[0];

        // Depth visualization.
		// Become brighter as approaching the clipping plane.
        float depth_color = -(float)nearest_pixel.depth /
		    sqrt(300 * 300 + 300 * 300);
        uav[dispatch_thread_id.xy] =
		    float4(depth_color, depth_color, depth_color, 1);
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void normals_main(uint3 group_id : SV_GroupID,
                  uint3 group_thread_id : SV_GroupThreadID,
                  uint3 dispatch_thread_id : SV_DispatchThreadID,
                  uint group_index : SV_GroupIndex) {
    uint32_t thread_x = group_thread_id.x; // Bounded in [0, 3] 
    uint32_t thread_y = group_thread_id.y; // Bounded in [0, 3]
    uint32_t thread_z = group_thread_id.z; // Bounded in [0, 3]
    uint32_t thread_bucket_index = thread_x + thread_y * 4;
    uint32_t current_cell = group_id.x + group_id.y * (480 / 4);
    bool thread_is_even = dispatch_thread_id.z % 2 == 0;

    sort_pixel_bucket(thread_x, thread_y, thread_z,
                      thread_bucket_index, current_cell,
					  thread_is_even);
 
    if (thread_z == 0) {
        // The first element is the lowest depth after sorting.
        Pixel nearest_pixel = gpu_buffer[0].cells[current_cell]
            .pixel_buckets[thread_bucket_index]
            .pixels[0];

        // Normals visualization.
        float3 normal_color = nearest_pixel.normal;
        uav[dispatch_thread_id.xy] = float4(normal_color, 1);
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
