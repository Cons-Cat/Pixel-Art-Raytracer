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
            short intersect_x_1 =
                static_cast<short>((position.x - ray.origin.x));
            short intersect_x_2 =
                static_cast<short>((position.x + extent.x - ray.origin.x));
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

template <int entity_count>
struct Entities {
    AABB aabbs[entity_count];
    Pixel colors[entity_count];  // TODO: Make sprite.
    int last_entity_index = 0;

    using Entity = struct {
        AABB aabb;
        Pixel color;
    };

    // TODO: Consider perfect forwarding or move:
    void insert(Entity const entity) {
        aabbs[last_entity_index] = entity.aabb;
        colors[last_entity_index] = entity.color;
        last_entity_index += 1;
    }

    auto size() -> int {
        return entity_count;
    }
};

constexpr short single_bin_size = 20;
constexpr int view_width = 480;
constexpr int view_height = 320;
constexpr int hash_width = view_width / single_bin_size;
// You can see bins as low along Y as the height of the view window.
constexpr int hash_height = view_height * 2 / single_bin_size;
constexpr int hash_length = view_height / single_bin_size;
constexpr int hash_cube_volume = hash_width * hash_height * hash_length;

constexpr int entity_count = 20;

// The spatial hash is organized near-to-far, by bottom-to-top, by
// left-to-right.
// That is generally a cache-friendly layout for this data.
auto index_into_view_hash(int x, int y, int z) -> int {
    return (x * hash_height * hash_length) + (y * hash_length) + z;
}

void count_entities_in_bins(Entities<entity_count>* p_entities,
                            AABB* p_aabb_bins, int* p_aabb_count_in_bin,
                            int* p_aabb_index_to_entity_index_map) {
    for (int i = 0; i < p_entities->size(); i++) {
        AABB& this_aabb = p_entities->aabbs[i];

        int min_x_world = this_aabb.position.x;
        int min_y_world = this_aabb.position.y;
        int min_z_world = this_aabb.position.z;
        int max_x_world = min_x_world + this_aabb.extent.x;
        int max_y_world = min_y_world + this_aabb.extent.y;
        int max_z_world = min_z_world + this_aabb.extent.z;

        // Skip this entity if it is outside the view bounds.
        if ((max_x_world < 0) || (min_x_world > view_width) ||
            (max_y_world < -view_height) || (min_y_world > view_height) ||
            (max_z_world < 0) || (min_z_world > view_height)) {
            break;
        }

        // Get the cells that this `AABB` fits into.
        int min_x_index = std::max(0, min_x_world / single_bin_size);
        int min_y_index =
            std::max(0, (min_y_world + view_height) / single_bin_size);
        int min_z_index = std::max(0, min_z_world / single_bin_size);

        // TODO: Figure out why `+ 2` is necessary.
        int max_x_index =
            std::min(hash_width, max_x_world / single_bin_size + 2);
        int max_y_index = std::min(
            hash_height, (max_y_world + view_height) / single_bin_size + 2);
        int max_z_index =
            std::min(hash_length, max_z_world / single_bin_size + 2);

        // Place this AABB into every bin that it spans across.
        for (int bin_x = min_x_index; bin_x <= max_x_index; bin_x += 1) {
            for (int bin_y = min_y_index; bin_y <= max_y_index; bin_y += 1) {
                for (int bin_z = min_z_index; bin_z <= max_z_index;
                     bin_z += 1) {
                    p_aabb_bins[index_into_view_hash(bin_x, bin_y, bin_z)] =
                        this_aabb;
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)] += 1;
                    p_aabb_index_to_entity_index_map[index_into_view_hash(
                        bin_x, bin_y, bin_z)] = i;
                }
            }
        }
    }
};

void trace_hash(Entities<entity_count>* p_entities, AABB* p_aabb_bins,
                int* p_aabb_count_in_bin, int* p_aabb_index_to_entity_index_map,
                Pixel* p_texture) {
    // `i` is a ray's world-position lateral to the ground, iterating
    // rightwards.
    for (short i = 0; i < view_width; i++) {
        // `j` is a ray's world-position skywards, iterating downwards.
        for (short j = 0; j < view_height; j++) {
            Ray this_ray = {
                .origin =
                    {
                        .x = i,
                        .y = static_cast<short>(view_height - j),
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

            int bin_x = static_cast<short>(i / single_bin_size);

            // `k` is a ray's hash-space position casting forwards.
            for (short k = 0; k < hash_length; k++) {
                int bin_z = k;
                // Y decreases as Z increases.
                int bin_y =
                    static_cast<short>(hash_height - (j / single_bin_size)) - k;

                // short closest_entity_depth =
                // std::numeric_limits<short>::max();

                int entities_in_this_bin =
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)];
                for (int this_bin_entity_index = 0;
                     this_bin_entity_index <
                     p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                              bin_z)];
                     this_bin_entity_index++) {
                    AABB& this_aabb =
                        p_aabb_bins[index_into_view_hash(bin_x, bin_y, bin_z) +
                                    this_bin_entity_index];

                    // TODO: If this entity has closer depth.
                    // if (this_aabb.min_point.y + (j -
                    // this_aabb.min_point.y) > closest_entity_depth) {
                    // }

                    // Intersect ray with this aabb.
                    if (this_aabb.position.x <= i &&
                        this_aabb.position.x + this_aabb.extent.x >= i) {
                        if (this_aabb.intersect(this_ray)) {
                            this_color =
                                p_entities
                                    ->colors[p_aabb_index_to_entity_index_map
                                                 [index_into_view_hash(
                                                     bin_x, bin_y, bin_z)]];
                            // TODO: Update `closest_entity_depth`.
                            has_intersected = true;
                        }
                    }
                }

                // Do not bother tracing this ray further if something
                // has already intersected.
                if (has_intersected) {
                    break;
                }
            }

            // `j` increases as the cursor moves downwards.
            // `i` increases as the cursor moves rightwards.
            p_texture[j * view_width + i] = this_color;
        }
    }

    // Draw hash grid.
    for (int i = 0; i < view_width; i += 1) {
        for (int j = 0; j < view_height; j += single_bin_size) {
            p_texture[j * view_width + i] = {0, 0, 0};
        }
    }
    for (int i = 0; i < view_width; i += single_bin_size) {
        for (int j = 0; j < view_height; j += 1) {
            p_texture[j * view_width + i] = {0, 0, 0};
        }
    }
};

auto main() -> int {
    auto p_entities = new (std::nothrow) Entities<entity_count>;

    // p_entities is random-access, but the bins they're stored into are not, so
    // we must store a random-access map to the entities' attributes.
    int* p_aabb_index_to_entity_index_map =
        new (std::nothrow) int[hash_cube_volume];

    // Track how many entities fit into each bin.
    int* p_aabb_count_in_bin = new (std::nothrow) int[hash_cube_volume];

    AABB* p_aabb_bins =
        new (std::nothrow) AABB[hash_width * hash_height * hash_length];
    int entity_count_currently_in_bin[hash_width * hash_height * hash_length];

    Pixel* p_texture = new (std::nothrow) Pixel[view_height * view_width];
    if (p_texture == nullptr) {
        return 1;
    }

    srand(time(0));

    for (int i = 0; i < p_entities->size(); i++) {
        // Place entities, in world-space, randomly throughout a cube which
        // bounds the orthographic view frustrum, assuming the camera is at
        // <0,0,0>.
        int x = (rand() % (view_width));
        // Y is between `+view_height` and `-view_height`.
        int y = (rand() % (view_height * 2)) - view_height;
        int z = (rand() % (view_height));

        Point<short> new_position = {static_cast<short>(x),
                                     static_cast<short>(y),
                                     static_cast<short>(z)};

        // An AABB's volume is currently hard-coded to 10^3.
        p_entities->insert({
            .aabb = {.position = {new_position.x, new_position.y,
                                  new_position.z},
                     .extent = {20, 20, 20}},

            // Randomize colors:
            .color = {static_cast<unsigned char>(rand()),
                      static_cast<unsigned char>(rand()),
                      static_cast<unsigned char>(255u)},

            // Visualize Y coordinates:
            // .color = {static_cast<unsigned char>((y + view_height) /
            //                                      (view_height * 2.f) *
            //                                      255.f),
            //           static_cast<unsigned char>((y + view_height) /
            //                                      (view_height * 2.f) *
            //                                      255.f),
            //           static_cast<unsigned char>((y + view_height) /
            //                                      (view_height * 2.f) *
            //                                      255.f)},
        });
    }

    // Place player character near the center.
    p_entities->aabbs[0].position = {view_width / 2, 10, view_height / 2};

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
                            p_entities->aabbs[0].position.x -= 10;
                            break;
                        case SDLK_RIGHT:
                            p_entities->aabbs[0].position.x += 10;
                            break;
                        case SDLK_UP:
                            p_entities->aabbs[0].position.z += 10;
                            break;
                        case SDLK_DOWN:
                            p_entities->aabbs[0].position.z -= 10;
                            break;
                        default:
                            break;
                    }
                    break;
            }
        }

        memset(p_aabb_count_in_bin, 0, hash_cube_volume * 4);
        count_entities_in_bins(p_entities, p_aabb_bins, p_aabb_count_in_bin,
                               p_aabb_index_to_entity_index_map);
        trace_hash(p_entities, p_aabb_bins, p_aabb_count_in_bin,
                   p_aabb_index_to_entity_index_map, p_texture);

        int texture_pitch;
        SDL_LockTexture(p_sdl_texture, nullptr, p_blit_address, &texture_pitch);
        for (int row = 0; row < view_height; row++) {
            memset(static_cast<char*>((void*)p_blit) + row * texture_pitch, 122,
                   1920);
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
