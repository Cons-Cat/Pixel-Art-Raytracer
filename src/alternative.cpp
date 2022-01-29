#include <liblava/lava.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_video.h>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

#include "./sprites.hpp"

template <typename T>
struct Point {
    T x, y, z;
};

struct Ray {
    Point<short> origin;
    Point<short> direction_inverse;
};
static_assert(sizeof(Ray) == 12);

struct alignas(16) AABB {
    // TODO: Factor into min_bound and max_bound, and update velocity with SIMD.
    Point<short> position;
    Point<short> extent;

    auto intersect(Ray& ray) -> bool {
        // Adapted from Fast, Branchless Ray/Bounding Box Intersections:
        // https://tavianator.com/2011/ray_box.html X plane comparisons.
        short min_distance = std::numeric_limits<short>::min();
        short max_distance = std::numeric_limits<short>::max();

        // X plane comparisons.
        if (ray.direction_inverse.x != 0) {
            short intersect_x_1 = static_cast<short>(
                (position.x - ray.origin.x) * ray.direction_inverse.x);
            short intersect_x_2 =
                static_cast<short>((position.x + extent.x - ray.origin.x) *
                                   ray.direction_inverse.x);
            min_distance = std::min(intersect_x_1, intersect_x_2);
            max_distance = std::max(intersect_x_1, intersect_x_2);
        }

        // Y plane comparisons.
        if (ray.direction_inverse.y != 0) {
            short intersect_y_1 = static_cast<short>(
                (position.y - ray.origin.y) * ray.direction_inverse.y);
            short intersect_y_2 =
                static_cast<short>((position.y + extent.y - ray.origin.y) *
                                   ray.direction_inverse.y);
            min_distance =
                std::max(min_distance, std::min(intersect_y_1, intersect_y_2));
            max_distance =
                std::min(max_distance, std::max(intersect_y_1, intersect_y_2));
        }

        // Z plane comparisons.
        if (ray.direction_inverse.z != 0) {
            short intersect_z_1 = static_cast<short>(
                (position.z - ray.origin.z) * ray.direction_inverse.z);
            short intersect_z_2 =
                static_cast<short>((position.z + extent.z - ray.origin.z) *
                                   ray.direction_inverse.z);
            min_distance =
                std::max(min_distance, std::min(intersect_z_1, intersect_z_2));
            max_distance =
                std::min(max_distance, std::max(intersect_z_1, intersect_z_2));
        }

        return max_distance >= min_distance;
    }
};

// Alignment pads this out from 12 bytes to 16.
static_assert(sizeof(AABB) == 16);

constexpr Sprite tile_single = make_tile_floor();

template <int entity_count>
struct Entities {
    std::vector<AABB> aabbs;
    std::vector<Sprite> sprites;

    int last_entity_index = 0;

    using Entity = struct {
        AABB aabb;
        Sprite sprite;
    };

    // TODO: Consider perfect forwarding or move:
    void insert(Entity const entity) {
        aabbs.push_back(entity.aabb);
        sprites.push_back(tile_single);
        last_entity_index += 1;
    }

    auto size() -> int {
        return last_entity_index;
    }
};

// The area of a bin's face is hard-coded to 20 to perfectly conform to the area
// of a tile's sprite.
constexpr short single_bin_area = 20;

constexpr int view_width = 480;
constexpr int view_height = 320;
constexpr int view_length = 320;
constexpr int hash_width = (view_width) / single_bin_area;
constexpr int hash_height = (view_height) / single_bin_area;
constexpr int hash_length = (view_length) / single_bin_area;
constexpr int hash_volume = hash_width * hash_height * hash_length;

// Currently, this number is no-op.
constexpr int entity_count = view_width * view_length;

// The number of AABBs that can fit inside of a single bin. This is an
// exponentiation of `2` for pushing into a bin with efficient wrapping
// semantics with bitwise `&`.
constexpr int sparse_bin_size = 8;

// The spatial hash is organized near-to-far, by bottom-to-top, by
// left-to-right.
// That is generally a cache-friendly layout for this data.
auto index_into_view_hash(int x, int y, int z) -> int {
    return (x * hash_height * hash_length) + (y * hash_length) + z;
}

auto world_to_view_hash_index(int x, int y, int z) -> int {
    int int_x = std::max(0, std::min(view_width, x / single_bin_area));
    int int_y = std::max(0, std::min(view_height, y / single_bin_area));
    int int_z = std::max(0, std::min(view_length, z / single_bin_area));
    return index_into_view_hash(int_x, int_y, int_z);
}

auto trace_hash_for_light(int* p_aabb_count_in_bin, int const bin_x_start,
                          int const bin_y_start, int const bin_z_start,
                          int const bin_x_end, int const bin_y_end,
                          int const bin_z_end) -> bool {
    // TODO: Benchmark against integer solution.
    Point<float> p1 = {static_cast<float>(bin_x_start),
                       static_cast<float>(bin_y_start),
                       static_cast<float>(bin_z_start)};
    Point<float> p2 = {
        static_cast<float>(bin_x_end), static_cast<float>(bin_y_end),
        static_cast<float>(bin_z_end),
        // std::min<float>(hash_width - 1.f, static_cast<float>(bin_x_end)),
        // std::min<float>(hash_height - 1.f, static_cast<float>(bin_y_end)),
        // std::min<float>(hash_length - 1.f, static_cast<float>(bin_z_end))
    };
    Point<float> current_point = p1;
    Point<float> distance = {p2.x - p1.x, p2.y - p1.y, p2.z - p1.z};
    // TODO: This is an inefficient initializer list.
    float max_component_distance = std::max<float>(
        {std::abs(distance.x), std::abs(distance.y), std::abs(distance.z)});
    Point<float> step_size = {distance.x / max_component_distance,
                              distance.y / max_component_distance,
                              distance.z / max_component_distance};
    for (float ii = 0; ii < max_component_distance; ii += 1.f) {
        current_point = {current_point.x + step_size.x,
                         current_point.y + step_size.y,
                         current_point.z + step_size.z};
        if (current_point.x >= view_width ||
            current_point.y >= view_height + view_length ||
            current_point.z >= view_height + view_length) {
            break;
        }
        if (p_aabb_count_in_bin[index_into_view_hash(
                static_cast<int>(current_point.x),
                static_cast<int>(current_point.y),
                static_cast<int>(current_point.z))] > 0) {
            return true;
        }
    }

    return false;
}

// TODO total aabb count.
AABB* p_aabb_flatbins = new (std::nothrow) AABB[hash_volume];
int total_entities_in_bins = 0;

void count_entities_in_bins(Entities<entity_count>* p_entities,
                            AABB* p_aabb_bins, int* p_aabb_count_in_bin,
                            int* p_aabb_index_to_entity_index_map) {
    for (int i = 0; i < p_entities->size(); i++) {
        AABB& this_aabb = p_entities->aabbs[i];

        // The `y` coordinate shifts upwards as `z` increases.
        int this_min_x_world = this_aabb.position.x;
        int this_min_y_world = this_aabb.position.y;
        int this_min_z_world = this_aabb.position.z;

        int this_max_x_world = this_min_x_world + this_aabb.extent.x;
        int this_max_y_world = this_min_y_world + this_aabb.extent.y;
        int this_max_z_world = this_min_z_world + this_aabb.extent.z;

        // TODO: Fix hard-coded numbers.
        // Skip this entity if it fits entirely outside of the view bounds.
        if ((this_max_x_world < 0) || (this_min_x_world >= view_width) ||
            (this_max_y_world < 0 - this_max_z_world) ||
            (this_min_y_world >= view_height - this_min_z_world + 20) ||
            (this_max_z_world < -this_aabb.extent.z - 20) ||
            (this_min_z_world > view_length + 20)) {
            continue;
        }

        // Get the cells that this `AABB` fits into.
        int min_x_index = std::max(0, this_min_x_world / single_bin_area);
        int min_y_index = std::max<int>(
            0, (view_height - this_max_y_world - this_max_z_world) /
                   single_bin_area);
        int min_z_index = std::max(0, this_min_z_world / single_bin_area);

        int max_x_index =
            std::min(hash_width - 1, this_max_x_world / single_bin_area);
        int max_y_index = std::min(
            // `max_y_index` is rounded up to the nearest multiple of `20`.
            hash_height,
            (view_height - this_min_y_world - this_min_z_world + 19) /
                single_bin_area);
        // `max_z_index` is rounded up to the nearest multiple of `20`.
        int max_z_index =
            std::min(hash_length, (this_max_z_world + 19) / single_bin_area);

        // Place this AABB into every bin that it spans across.
        for (int bin_x = min_x_index; bin_x <= max_x_index; bin_x += 1) {
            for (int bin_y = min_y_index; bin_y < max_y_index; bin_y += 1) {
                for (int bin_z = min_z_index; bin_z < max_z_index; bin_z += 1) {
                    int this_bin_count =
                        p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                                 bin_z)];

                    p_aabb_index_to_entity_index_map[index_into_view_hash(
                                                         bin_x, bin_y, bin_z) *
                                                         sparse_bin_size +
                                                     this_bin_count] = i;

                    p_aabb_bins[index_into_view_hash(bin_x, bin_y, bin_z) *
                                    sparse_bin_size +
                                this_bin_count] = this_aabb;

                    // Increment the count of `AABB`s in this bin, wrapping
                    // around `sparse_bin_size`. That value is currently
                    // `8`.
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)] =
                        (this_bin_count + 1) & (sparse_bin_size - 1);
                }
            }
        }
    }
};

void trace_hash_for_color(Entities<entity_count>* p_entities, AABB* p_aabb_bins,
                          int* p_aabb_count_in_bin,
                          int* p_aabb_index_to_entity_index_map,
                          Pixel* p_texture) {
    // `i` is a ray's `x` world-position ground, iterating
    // rightwards.
    for (short i = 0; i < view_width; i++) {
        // `j` is a ray's `y` world-position, iterating upwards.
        for (short j = 0; j < view_height; j++) {
            short world_j = static_cast<short>(view_height - j);
            Ray this_ray = {
                .origin =
                    {
                        .x = i,
                        .y = world_j,
                        .z = 0,
                    },
                .direction_inverse =
                    {
                        .x = 0,
                        .y = -1,  // 1 / -1
                        .z = 1,   // 1 / 1
                    },
            };

            Pixel this_color = {.color = {255 / 2, 255 / 2, 255 / 2}};
            int intersected_bin_count = 0;

            // The hash frustrum's data is stored such that increasing the
            // `z` index finds AABBs with proportionally lower `y`
            // coordinates, so decrementing `y` by `z` here is unnecessary.
            int bin_x = i / single_bin_area;

            int closest_entity_depth = std::numeric_limits<int>::min();

            // `bin_z` is a ray's hash-space position casting forwards.
            for (short bin_z = 0; bin_z < hash_length; bin_z++) {
                bool has_intersected = false;
                short bin_y = static_cast<short>(j / single_bin_area);

                int entities_in_this_bin =
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)];
                if (entities_in_this_bin == 0) {
                    intersected_bin_count = 0;
                }

                for (int this_bin_entity_index = 0;
                     this_bin_entity_index < entities_in_this_bin;
                     this_bin_entity_index += 1) {
                    AABB& this_aabb =
                        p_aabb_bins[index_into_view_hash(bin_x, bin_y, bin_z) *
                                        sparse_bin_size +
                                    this_bin_entity_index];

                    // Intersect ray with this aabb.
                    if (i >= this_aabb.position.x &&
                        i < this_aabb.position.x + this_aabb.extent.x) {
                        if (world_j >
                                this_aabb.position.y + this_aabb.position.z &&
                            world_j <=
                                this_aabb.position.y + this_aabb.extent.y +
                                    this_aabb.position.z + this_aabb.extent.z) {
                            if (this_aabb.intersect(this_ray)) {
                                int this_entity_index =
                                    p_aabb_index_to_entity_index_map
                                        [index_into_view_hash(bin_x, bin_y,
                                                              bin_z) *
                                             sparse_bin_size +
                                         this_bin_entity_index];

                                Sprite& this_sprite =
                                    p_entities->sprites[this_bin_entity_index];

                                int sprite_px_row =
                                    this_aabb.position.y + this_aabb.extent.y +
                                    this_aabb.position.z + this_aabb.extent.z -
                                    world_j;

                                int this_sprite_px_index =
                                    sprite_px_row * 20 +
                                    // Sprite pixel's column:
                                    (i - this_aabb.position.x);

                                // Depth increases as `y` increases, and it
                                // decreases as `z` increases.
                                int this_depth =
                                    this_aabb.position.y -
                                    this_aabb.position.z
                                    // Position along this `AABB`'s `y`
                                    // axis:
                                    + std::min<int>(
                                          0, this_aabb.extent.y - sprite_px_row)
                                    // Position along this `AABB`'s `z`
                                    // axis:
                                    - this_sprite.depth[this_sprite_px_index];

                                // Store the pixel with the greatest depth.
                                if (closest_entity_depth >= this_depth) {
                                    continue;
                                }
                                closest_entity_depth = this_depth;

                                this_color.color = color_palette
                                    [this_sprite.color[this_sprite_px_index]];
                                this_color.y = world_j;
                                this_color.z =
                                    this_aabb.position.z +
                                    this_sprite.depth[this_sprite_px_index];
                                this_color.normal =
                                    this_sprite.normal[this_sprite_px_index];

                                has_intersected = true;
                            }
                        }
                    }
                }
                intersected_bin_count += has_intersected;

                // Do not bother tracing this ray further if it has
                // intersected two adjacent bins already.
                if (intersected_bin_count >= 2) {
                    goto escape_ray;
                }
            }

escape_ray:
            // `j` decreases as the cursor moves downwards.
            // `i` increases as the cursor moves rightwards.
            p_texture[j * view_width + i] = this_color;
        }
    }

    // Draw hash grid.
    for (int i = 0; i < view_width; i += 1) {
        for (int j = 0; j < view_height; j += single_bin_area) {
            p_texture[j * view_width + i] = {0, 0, 0};
        }
    }
    for (int i = 0; i < view_width; i += single_bin_area) {
        for (int j = 0; j < view_height; j += 1) {
            p_texture[j * view_width + i] = {0, 0, 0};
        }
    }
};

auto main() -> int {
    int* p_aabb_index_to_entity_index_map =
        new (std::nothrow) int[hash_volume * sparse_bin_size];

    // Track how many entities fit into each bin.
    int* p_aabb_count_in_bin = new (std::nothrow) int[hash_volume];

    AABB* p_aabb_bins = new (std::nothrow) AABB[hash_volume * sparse_bin_size];

    Pixel* p_pixel_buffer = new (std::nothrow) Pixel[view_height * view_width];
    if (p_pixel_buffer == nullptr) {
        return 1;
    }
    Color* p_texture = new (std::nothrow) Color[view_height * view_width];

    auto p_entities = new (std::nothrow) Entities<entity_count>;

    // Insert player:
    p_entities->insert({
        .aabb = {.position = {view_width / 2, 36, view_length / 4},
                 .extent = {20, 20, 20}},
    });

    // Create graybox world.
    {
        for (int i = 0; i < view_width; i++) {
            for (int j = 0; j < view_length; j++) {
                int x = i * 20;
                int y = 0;
                int z = j * 20;

                if (x >= view_width / 2 - 40 && x < view_width / 2 + 40 &&
                    z < view_length / 2 + 40 && z > view_length / 2 - 40) {
                    continue;
                }

                Point<short> new_position = {static_cast<short>(x),
                                             static_cast<short>(y),
                                             static_cast<short>(z)};
                p_entities->insert({
                    .aabb = {.position = {new_position.x, new_position.y,
                                          new_position.z},
                             .extent = {20, 20, 20}},
                });
            }
        }

        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < view_length - 10; j++) {
                for (int k = 1; k < 6; k++) {
                    if (i >= 4 && k >= 4) {
                        continue;
                    }
                    int x = i * 20;
                    int y = k * 20;
                    int z = view_length - j * 20;
                    Point<short> new_position = {static_cast<short>(x),
                                                 static_cast<short>(y),
                                                 static_cast<short>(z)};
                    p_entities->insert({
                        .aabb = {.position = {new_position.x, new_position.y,
                                              new_position.z},
                                 .extent = {20, 20, 20}},
                    });
                }
            }
        }

        for (int i = 1; i < 3; i++) {
            for (int j = 0; j < view_length; j++) {
                int x = view_width - i * 20;
                int y = 20;
                int z = j * 20;
                Point<short> new_position = {static_cast<short>(x),
                                             static_cast<short>(y),
                                             static_cast<short>(z)};
                p_entities->insert({
                    .aabb = {.position = {new_position.x, new_position.y,
                                          new_position.z},
                             .extent = {20, 20, 20}},
                });
            }
        }

        for (int i = 1; i < 20; i++) {
            int x = view_width - 40 - i * 20;
            int y = 20;
            int z = view_length - 60;
            Point<short> new_position = {static_cast<short>(x),
                                         static_cast<short>(y),
                                         static_cast<short>(z)};
            p_entities->insert({
                .aabb = {.position = {new_position.x, new_position.y,
                                      new_position.z},
                         .extent = {20, 20, 20}},
            });
        }
    }

    // TODO: Make a trivial pass-through graphics shader pipeline in
    // Vulkan to render texture.

    SDL_InitSubSystem(SDL_INIT_VIDEO);

    SDL_Window* p_window =
        SDL_CreateWindow(nullptr, SDL_WINDOWPOS_UNDEFINED,
                         SDL_WINDOWPOS_UNDEFINED, view_width, view_height, 0);
    SDL_Renderer* p_renderer =
        SDL_CreateRenderer(p_window, -1, SDL_RENDERER_SOFTWARE);

    SDL_Texture* p_sdl_texture =
        SDL_CreateTexture(p_renderer, SDL_PIXELFORMAT_RGB888,
                          SDL_TEXTUREACCESS_STREAMING, view_width, view_height);

    Color* p_blit = new (std::nothrow) Color[view_width * view_height];
    void** p_blit_address = static_cast<void**>(static_cast<void*>(&p_blit));

    using Light = struct Light {
        short x, y, z;
        short radius = 10;
    };

    std::vector<Light> lights;
    lights.push_back(
        {.x = view_width, .y = view_height / 5, .z = view_length / 2});

    while (true) {
        SDL_Event event;
        if (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYUP:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            goto exit_loop;
                            break;
                        case SDLK_a:
                            lights[0].z -= 5;
                            break;
                        case SDLK_k:
                            lights[0].z += 5;
                            break;
                    }
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_LEFT:
                            p_entities->aabbs[0].position.x -= 5;
                            break;
                        case SDLK_RIGHT:
                            p_entities->aabbs[0].position.x += 5;
                            break;
                        case SDLK_UP:
                            p_entities->aabbs[0].position.z += 5;
                            break;
                        case SDLK_DOWN:
                            p_entities->aabbs[0].position.z -= 5;
                            break;
                        case SDLK_PAGEDOWN:
                            p_entities->aabbs[0].position.y -= 5;
                            break;
                        case SDLK_PAGEUP:
                            p_entities->aabbs[0].position.y += 5;
                            break;
                        default:
                            break;
                    }
                    break;
            }
        }

        // Reset bin counts to `0`.
        memset(p_aabb_count_in_bin, 0,
               hash_volume * sizeof(decltype(*p_aabb_count_in_bin)));
        count_entities_in_bins(p_entities, p_aabb_bins, p_aabb_count_in_bin,
                               p_aabb_index_to_entity_index_map);
        trace_hash_for_color(p_entities, p_aabb_bins, p_aabb_count_in_bin,
                             p_aabb_index_to_entity_index_map, p_pixel_buffer);

        float ambient_light = 0.25f;
        for (int i = 0; i < view_height * view_width; i++) {
            Pixel& this_pixel = p_pixel_buffer[i];
            Vector normal = this_pixel.normal;

            int world_x = i % view_width;
            int world_y = this_pixel.y;
            int world_z = this_pixel.z;

            // TODO: This isn't actually the incident vector.
            Vector towards_light =
                Vector{.x = static_cast<float>(lights[0].x - world_x),
                       .y = -static_cast<float>(lights[0].y - world_y),
                       .z = static_cast<float>(lights[0].z - world_z)}
                    .normalize();

            Ray this_ray = {
                .origin = {static_cast<short>(world_x),
                           static_cast<short>(world_y),
                           static_cast<short>(world_z)},
                .direction_inverse = {
                    .x = static_cast<short>(1.f / towards_light.x),
                    .y = static_cast<short>(1.f / towards_light.y),
                    .z = static_cast<short>(1.f / towards_light.z)}};

            int ray_bin_x = world_x / single_bin_area;
            int ray_bin_y = (view_height - world_y - world_z) / single_bin_area;
            int ray_bin_z = world_z / single_bin_area;

            int light_bin_x = lights[0].x / single_bin_area;
            int light_bin_y =
                (view_height - lights[0].y - lights[0].z) / single_bin_area;
            int light_bin_z = lights[0].z / single_bin_area;

            // Set the texture to an ambient brightness by default.
            p_texture[i] = this_pixel.color * ambient_light;

            if (trace_hash_for_light(p_aabb_count_in_bin, ray_bin_x, ray_bin_y,
                                     ray_bin_z, light_bin_x, light_bin_y,
                                     light_bin_z)) {
                continue;
            }

            // Get the dot product between this pixel's normal and the light
            // ray's incident vector.
            float diffuse = std::max<float>(0, normal.x * towards_light.x +
                                                   normal.y * towards_light.y +
                                                   normal.z * towards_light.z);
            // Multiply diffuse by distance to the light source.
            // * (static_cast<float>(std::abs(world_x - lights[0].x) +
            //                       std::abs(world_y - lights[0].y) +
            //                       std::abs(world_z - lights[0].z)) /
            //    200.f);

            p_texture[i] = this_pixel.color *
                           std::min<float>(1.f, diffuse + ambient_light);
        }

        int texture_pitch;
        SDL_LockTexture(p_sdl_texture, nullptr, p_blit_address, &texture_pitch);
        for (int row = 0; row < view_height; row++) {
            memcpy(static_cast<char*>(static_cast<void*>(p_blit)) +
                       row * texture_pitch,
                   p_texture + row * view_width, view_width * sizeof(Color));
        }
        SDL_UnlockTexture(p_sdl_texture);

        SDL_Rect view_rect = {0, 0, view_width, view_height};
        SDL_Rect blit_rect = {
            0, 0, static_cast<int>(texture_pitch / sizeof(Color)), view_height};

        SDL_RenderCopy(p_renderer, p_sdl_texture, &view_rect, &blit_rect);
        SDL_RenderPresent(p_renderer);

#ifndef __OPTIMIZE__
        std::cout << "<" << p_entities->aabbs[0].position.x << ", "
                  << p_entities->aabbs[0].position.y << ", "
                  << p_entities->aabbs[0].position.z << ">\n";
        std::cout
            << "<"
            << p_entities->aabbs[0].position.x + p_entities->aabbs[0].extent.x
            << ", "
            << p_entities->aabbs[0].position.y + p_entities->aabbs[0].extent.y
            << ", "
            << p_entities->aabbs[0].position.z + p_entities->aabbs[0].extent.z
            << ">\n";

        for (int j = 0; j < hash_height; j++) {
            for (int k = 0; k < hash_length; k++) {
                std::cout
                    << p_aabb_count_in_bin[index_into_view_hash(
                           p_entities->aabbs[0].position.x / single_bin_area, j,
                           k)]
                    << " ";
            }
            std::cout << "\n";
        }
#endif

        static unsigned int last_time = 0u;
        std::cout << SDL_GetTicks() - last_time << "ms\n\n";
        last_time = SDL_GetTicks();
    }

exit_loop:
    SDL_DestroyTexture(p_sdl_texture);
    SDL_DestroyWindow(p_window);
    SDL_DestroyRenderer(p_renderer);
    SDL_VideoQuit();

    delete[] p_aabb_bins;
    // Segfaults:
    // delete[] p_aabb_count_in_bin;
    delete p_entities;
    delete[] p_aabb_index_to_entity_index_map;
    // Segfaults:
    // delete[] p_blit;
}

/* Create an entity.
 * Create a bounding volume hierarchy of AABBs.
 * Give entities an Oklab value.
 *
 * Iterate through every pixel in window:
 ** Travel to first AABB bounding an entity.
 ** Assign pixel to that entity's color.
 *
 * Iterate through every pixel in window:
 ** Convert pixel color to RGB.
 *
 * Stage pixel buffer memory.
 * Invoke trivial rasterization pipeline. */
