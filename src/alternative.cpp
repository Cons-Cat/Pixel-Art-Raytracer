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
#include <string>

template <typename Int>
struct Point {
    Int x, y, z;
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

struct Pixel {
    unsigned char red, green, blue, alpha;
};

Pixel pixel_palette[] = {
    {60, 60, 60},     // Dark
    {120, 120, 120},  // Dark gray
    {200, 200, 200},  // Bright gray
    {240, 240, 240},  // Bright
};

struct Sprite {
    int data[20 * 40];
};

constexpr auto make_tile_floor = []() -> Sprite {
    constexpr Sprite data{
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,  //
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  //
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  //
    };
    return data;
};
constexpr Sprite tile_floor = make_tile_floor();

// constexpr auto make_tile_front = []() -> Sprite {
//     return {};
// };
// constexpr Sprite tile_front = make_tile_floor();

template <int entity_count>
struct Entities {
    AABB aabbs[entity_count];
    Sprite sprites[entity_count];

    int last_entity_index = 0;

    using Entity = struct {
        AABB aabb;
        Sprite sprite;
    };

    // TODO: Consider perfect forwarding or move:
    void insert(Entity const entity) {
        aabbs[last_entity_index] = entity.aabb;
        // sprites[last_entity_index] = entity.sprite;
        sprites[last_entity_index] = tile_floor;
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

constexpr int entity_count = 200;

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
            (this_min_z_world >= view_length)) {
            continue;
        }

        // Get the cells that this `AABB` fits into.
        int min_x_index = std::max(0, this_min_x_world / single_bin_area);
        int min_y_index =
            std::max(0, (view_height - this_max_y_world) / single_bin_area -
                            (this_max_z_world) / single_bin_area);
        int min_z_index = std::max(0, this_min_z_world / single_bin_area) - 1;

        int max_x_index =
            std::min(hash_width - 1, this_max_x_world / single_bin_area);
        int max_y_index =
            std::min(hash_height - 1,
                     (view_height - this_min_y_world) / single_bin_area -
                         (this_min_z_world) / single_bin_area);
        int max_z_index =
            std::min(hash_length - 1, this_max_z_world / single_bin_area);

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
                    // around `sparse_bin_size`. That value is currently `8`.
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)] =
                        (this_bin_count + 1) & (sparse_bin_size - 1);
                }
            }
        }
    }
};

void trace_hash(Entities<entity_count>* p_entities, AABB* p_aabb_bins,
                int* p_aabb_count_in_bin, int* p_aabb_index_to_entity_index_map,
                Pixel* p_texture) {
    // `i` is a ray's `x` world-position ground, iterating
    // rightwards.
    for (short i = 0; i < view_width; i++) {
        // `j` is a ray's `y` world-position, iterating upwards.
        // for (short j = view_height - 1; j >= 0; j--) {
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

            Pixel this_color = {55, 55, 55};
            bool has_intersected = false;

            // The hash frustrum's data is stored such that increasing the `z`
            // index finds AABBs with proportionally lower `y` coordinates, so
            // decrementing `y` by `z` here is unnecessary.
            int bin_x = i / single_bin_area;

            // `bin_z` is a ray's hash-space position casting forwards.
            for (short bin_z = 0; bin_z < hash_length; bin_z++) {
                short bin_y = static_cast<short>(j / single_bin_area);

                // short closest_entity_depth =
                // std::numeric_limits<short>::max();

                int entities_in_this_bin =
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)];

                for (int this_bin_entity_index = 0;
                     this_bin_entity_index < entities_in_this_bin;
                     this_bin_entity_index += 1) {
                    AABB& this_aabb =
                        p_aabb_bins[index_into_view_hash(bin_x, bin_y, bin_z) *
                                        sparse_bin_size +
                                    this_bin_entity_index];

                    // TODO: If this entity has closer depth.
                    // if (this_aabb.min_point.y + (j -
                    // this_aabb.min_point.y) > closest_entity_depth) {
                    // }

                    // Intersect ray with this aabb.
                    if (i >= this_aabb.position.x &&
                        i < this_aabb.position.x + this_aabb.extent.x) {
                        if (this_aabb.intersect(this_ray)) {
                            int this_entity_index =
                                p_aabb_index_to_entity_index_map
                                    [index_into_view_hash(bin_x, bin_y, bin_z) *
                                         sparse_bin_size +
                                     this_bin_entity_index];

                            int this_sprite_index =
                                // Sprite's row:
                                ((p_entities->aabbs[this_entity_index]
                                      .position.y +
                                  p_entities->aabbs[this_entity_index]
                                      .extent.y +
                                  p_entities->aabbs[this_entity_index]
                                      .position.z +
                                  p_entities->aabbs[this_entity_index]
                                      .extent.z) -
                                 (world_j)) *
                                    20 +
                                // Sprite's column:
                                (i - p_entities->aabbs[this_entity_index]
                                         .position.x);

                            this_color =
                                pixel_palette[p_entities
                                                  ->sprites[this_entity_index]
                                                  .data[this_sprite_index]];

                            // this_color = {255, 255, 255};
                            // TODO: Update `closest_entity_depth`.
                            has_intersected = true;
                        }
                    }
                }

                // Do not bother tracing this ray further if something
                // has already intersected.
                if (has_intersected) {
                    goto escape_ray;
                }
escape_ray:
                continue;
            }

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
    auto p_entities = new (std::nothrow) Entities<entity_count>;

    int* p_aabb_index_to_entity_index_map =
        new (std::nothrow) int[hash_volume * sparse_bin_size];

    // Track how many entities fit into each bin.
    int* p_aabb_count_in_bin = new (std::nothrow) int[hash_volume];

    AABB* p_aabb_bins = new (std::nothrow) AABB[hash_volume * sparse_bin_size];

    Pixel* p_texture = new (std::nothrow) Pixel[view_height * view_width];
    if (p_texture == nullptr) {
        return 1;
    }

    srand(time(0));

    for (int i = 0; i < entity_count; i++) {
        // Place entities randomly in world-space, localized around <0,0,0>.
        int x = (rand() % (view_width * 2)) - view_width / 2;
        int y = (rand() % (view_height * 2)) - view_height / 2;
        int z = (rand() % (view_length * 2)) - view_length / 2;

        Point<short> new_position = {static_cast<short>(x),
                                     static_cast<short>(y),
                                     static_cast<short>(z)};

        // An AABB's volume is currently hard-coded to 20^3.
        p_entities->insert({
            .aabb = {.position = {new_position.x, new_position.y,
                                  new_position.z},
                     .extent = {20, 20, 20}},
        });
    }

    // Place player character near the center.
    p_entities->aabbs[0].position = {view_width / 2, 0, view_length / 4};

    // TODO: Make a trivial pass-through graphics shader pipeline in Vulkan
    // to render texture.

    SDL_InitSubSystem(SDL_INIT_VIDEO);

    SDL_Window* p_window =
        SDL_CreateWindow(nullptr, SDL_WINDOWPOS_UNDEFINED,
                         SDL_WINDOWPOS_UNDEFINED, view_width, view_height, 0);
    SDL_Renderer* p_renderer =
        SDL_CreateRenderer(p_window, -1, SDL_RENDERER_SOFTWARE);

    SDL_Texture* p_sdl_texture =
        SDL_CreateTexture(p_renderer, SDL_PIXELFORMAT_RGB888,
                          SDL_TEXTUREACCESS_STREAMING, view_width, view_height);

    Pixel* p_blit = new (std::nothrow) Pixel[view_width * view_height];
    void** p_blit_address = static_cast<void**>(static_cast<void*>(&p_blit));

    while (true) {
        SDL_Event event;
        if (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYUP:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            goto exit_loop;
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
        trace_hash(p_entities, p_aabb_bins, p_aabb_count_in_bin,
                   p_aabb_index_to_entity_index_map, p_texture);

        int texture_pitch;
        SDL_LockTexture(p_sdl_texture, nullptr, p_blit_address, &texture_pitch);
        for (int row = 0; row < view_height; row++) {
            // Reset texture to gray.
            memset(static_cast<char*>((void*)p_blit) + row * texture_pitch,
                   256 / 2, 1920);
            memcpy(static_cast<char*>((void*)p_blit) + row * texture_pitch,
                   p_texture + row * view_width, view_width * sizeof(Pixel));
        }
        SDL_UnlockTexture(p_sdl_texture);

        SDL_Rect view_rect = {0, 0, view_width, view_height};
        SDL_Rect blit_rect = {
            0, 0, static_cast<int>(texture_pitch / sizeof(Pixel)), view_height};

        // SDL_RenderClear(p_renderer);
        SDL_RenderCopy(p_renderer, p_sdl_texture, &view_rect, &blit_rect);
        SDL_RenderPresent(p_renderer);
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
