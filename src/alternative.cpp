#include <liblava/lava.hpp>

#include <SDL2/SDL.h>
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
    Point<short> direction;
    Point<short> direction_inverse;
};

struct alignas(16) AABB {
    Point<short> min_point;
    Point<short> max_point;

    auto get_center() -> Point<short> {
        // TODO: Remove these casts.
        return Point<short>{
            .x = static_cast<short>((min_point.x + max_point.x) / 2),
            .y = static_cast<short>((min_point.y + max_point.y) / 2),
            .z = static_cast<short>((min_point.z + max_point.z) / 2),
        };
    }

    auto intersect(Ray& ray) -> bool {
        short distance_x_1 = static_cast<short>((min_point.x - ray.origin.x) *
                                                ray.direction_inverse.x);
        short distance_x_2 = static_cast<short>((max_point.x - ray.origin.x) *
                                                ray.direction_inverse.x);

        short min_distance = std::min(distance_x_1, distance_x_2);
        short max_distance = std::max(distance_x_1, distance_x_2);

        short distance_y_1 = static_cast<short>((min_point.y - ray.origin.y) *
                                                ray.direction_inverse.y);
        short distance_y_2 = static_cast<short>((max_point.y - ray.origin.y) *
                                                ray.direction_inverse.y);

        min_distance =
            std::max(min_distance, std::min(distance_y_1, distance_y_2));
        max_distance =
            std::min(max_distance, std::max(distance_y_1, distance_y_2));

        return max_distance >= min_distance;
    }
};

// Alignment pads this out from 12 bytes to 16.
static_assert(sizeof(AABB) == 16);

struct Pixel {
    unsigned char red, green, blue;
};

template <int entity_count>
struct Entities {
    AABB aabbs[entity_count];
    Point<int> positions[entity_count];
    Pixel colors[entity_count];  // TODO: Make sprite.
    int last_entity_index = 0;

    using Entity = struct {
        AABB aabb;
        Point<int> position;
        Pixel color;
    };

    // TODO: Consider perfect forwarding or move:
    void insert(Entity const entity) {
        aabbs[last_entity_index] = entity.aabb;
        positions[last_entity_index] = entity.position;
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
constexpr int bin_count_in_hash_width = view_width / single_bin_size;
// You can see bins as low along Y as the height of the view window.
constexpr int bin_count_in_hash_height = view_height * 2 / single_bin_size;
constexpr int bin_count_in_hash_length = view_height / single_bin_size;

// The spatial hash is organized near-to-far, by bottom-to-top, by
// left-to-right.
// That is generally a cache-friendly layout for this data.
auto index_into_view_hash(int x, int y, int z) -> int {
    return (x * bin_count_in_hash_height * bin_count_in_hash_length) +
           (y * bin_count_in_hash_length) + z;
}

auto main() -> int {
    constexpr int entity_count = 20;
    auto p_entities = new (std::nothrow) Entities<entity_count>;

    for (int i = 0; i < p_entities->size(); i++) {
        // Place entities, in world-space, randomly throughout a cube which
        // bounds the orthographic view frustrum, assuming the camera is at
        // <0,0,0>.
        int x = (rand() % (view_width));
        int y = (rand() % (view_height * 2)) - view_height;
        int z = (rand() % (view_height));

        Point<int> new_position = {x, y, z};

        // An AABB's volume is currently hard-coded to 20^3.
        p_entities->insert({
            .aabb = {.min_point = {static_cast<short>(new_position.x),
                                   static_cast<short>(new_position.y),
                                   static_cast<short>(new_position.z)},
                     .max_point = {static_cast<short>(new_position.x + 20),
                                   static_cast<short>(new_position.y + 20),
                                   static_cast<short>(new_position.z + 20)}},
            .position = new_position,
            .color = {255u, 255u, 255u},
        });
    }

    int hash_cube_volume = bin_count_in_hash_width * bin_count_in_hash_height *
                           bin_count_in_hash_length;

    // p_entities is random-access, but the bins they're stored into are not, so
    // we must store a random-access map to the entities' attributes.
    auto p_aabb_index_to_entity_index_map =
        new (std::nothrow) int[hash_cube_volume];

    // Track how many entities fit into each bin.
    auto p_aabb_count_in_bin = new (std::nothrow) int[hash_cube_volume];

    auto p_aabb_bins = new (std::nothrow)
        AABB[bin_count_in_hash_width * bin_count_in_hash_height *
             bin_count_in_hash_length];
    int entity_count_currently_in_bin[bin_count_in_hash_width *
                                      bin_count_in_hash_height *
                                      bin_count_in_hash_length];

    // Determine the bin index of each entity AABB bin.
    // auto p_aabb_bin_index = new (std::nothrow) int[bins_cube_volume];

    for (int i = 0; i < p_entities->size(); i++) {
        AABB& this_aabb = p_entities->aabbs[i];

        // Get the cells that this AABB fits into.
        int min_x_index = this_aabb.min_point.x / single_bin_size;
        int min_y_index = this_aabb.min_point.y / single_bin_size;
        int min_z_index = this_aabb.min_point.z / single_bin_size;

        int max_x_index = this_aabb.max_point.x / single_bin_size;
        int max_y_index = this_aabb.max_point.y / single_bin_size;
        int max_z_index = this_aabb.max_point.z / single_bin_size;

        // Skip this entity if it is outside the view bounds.
        // TODO: Make this comparison is world-space before the division
        // operations.
        if (min_x_index < 0 || min_y_index < 0 || min_z_index < 0 ||
            max_x_index > bin_count_in_hash_width ||
            max_y_index > bin_count_in_hash_height ||
            max_z_index > bin_count_in_hash_width) {
            break;
        }

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

    Point<short> ray_direction = {
        .x = 0,
        .y = -1,
        .z = 1,
    };

    auto p_texture = new (std::nothrow) Pixel[view_height * view_width];

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
                // The direction in world-space is <0, -1, 1>
                // The direction in hash-space is <0, 1, 1>
                .direction_inverse =
                    {
                        .x = 0,
                        .y = 1,   // -1 / -1
                        .z = -1,  // 1 / -1
                    },
            };

            int bin_x = static_cast<short>(i / single_bin_size);

            // `k` is a ray's world-position casting forwards.
            for (short k = 0; k < bin_count_in_hash_length * single_bin_size;
                 k++) {
                int bin_z = static_cast<short>(k / single_bin_size);
                int bin_y = static_cast<short>(bin_count_in_hash_height -
                                               (j / single_bin_size));

                // If the ray's bin is below the view of the window, stop
                // travelling, to not index the texture out of bounds.
                //
                // TODO: Refactor so that there are no bins out of view.
                if (j + k > view_height) {
                    goto break_pixel;
                }

                // short closest_entity_depth =
                // std::numeric_limits<short>::max();

                int entities_in_this_bin =
                    p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                             bin_z)];
                Pixel background_color = {25, 25, 25};

                for (int this_bin_entity_index = 0;
                     this_bin_entity_index <
                     p_aabb_count_in_bin[index_into_view_hash(bin_x, bin_y,
                                                              bin_z)];
                     this_bin_entity_index++) {
                    AABB& this_aabb =
                        p_aabb_bins[index_into_view_hash(bin_x, bin_y, bin_z) +
                                    this_bin_entity_index];

                    // TODO: If this entity has closer depth.
                    // if (this_aabb.min_point.y + (j - this_aabb.min_point.y) >
                    // closest_entity_depth) {
                    // }

                    // TODO: Remove this:
                    background_color =
                        p_entities->colors[p_aabb_index_to_entity_index_map
                                               [index_into_view_hash(
                                                   bin_x, bin_y, bin_z)]];

                    // Intersect ray with this aabb.
                    if (this_aabb.intersect(this_ray)) {
                        background_color =
                            p_entities->colors[p_aabb_index_to_entity_index_map
                                                   [index_into_view_hash(
                                                       bin_x, bin_y, bin_z)]];
                        break;

                        // TODO: Update `closest_entity_depth`.
                        // TODO: Kill ray after intersecting with any AABBs
                        // in a bin.
                    }
                }

                // `j` and `k` increase as the cursor moves downwards.
                // `i` increases as the cursor moves rightwards.
                p_texture[j * view_width + i] = background_color;
            }
        }
break_pixel:
        continue;
    }

    for (int j = 0; j < view_height; j++) {
        std::cout << "Row " << j << ": ";
        for (int i = 0; i < view_width; i++) {
            int col = p_texture[j * view_width + i].blue;
            if (col == 25) {
                col = 0;
            }
            std::cout << std::to_string(col) << ' ';
        }
        std::cout << "\n\n";
    }

    // TODO: Make a trivial pass-through graphics shader pipeline in Vulkan to
    // render texture.

    SDL_InitSubSystem(SDL_INIT_VIDEO);

    SDL_Window* p_window =
        SDL_CreateWindow(nullptr, SDL_WINDOWPOS_UNDEFINED,
                         SDL_WINDOWPOS_UNDEFINED, view_width, view_height, 0);
    SDL_Renderer* p_renderer =
        SDL_CreateRenderer(p_window, -1, SDL_RENDERER_SOFTWARE);

    SDL_Texture* p_sdl_texture =
        SDL_CreateTexture(p_renderer, SDL_PIXELFORMAT_RGB888,
                          SDL_TEXTUREACCESS_STREAMING, view_width, view_height);

    // `texture_row_size` is required by `SDL_LockTexture`, but it isn't used
    // for anything interesting.
    int texture_row_size;
    void* p_blit = new (std::nothrow) Pixel[view_width * view_height];
    SDL_LockTexture(p_sdl_texture, nullptr, &p_blit, &texture_row_size);
    memcpy(p_blit, p_texture, view_width * view_height * sizeof(Pixel));
    SDL_UnlockTexture(p_sdl_texture);

    while (true) {
        SDL_Event e;
        if (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_ESCAPE) {
                break;
            }
        }

        SDL_RenderCopy(p_renderer, p_sdl_texture, nullptr, nullptr);
        SDL_RenderPresent(p_renderer);
    }

    SDL_DestroyTexture(p_sdl_texture);
    SDL_DestroyWindow(p_window);
    SDL_DestroyRenderer(p_renderer);
    SDL_Quit();

    delete[] p_aabb_bins;
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
