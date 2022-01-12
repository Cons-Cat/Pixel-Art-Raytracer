#include <liblava/lava.hpp>

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
    Point<int16_t> origin;
    Point<int16_t> direction;
    Point<int16_t> direction_inverse;
};

struct alignas(16) AABB {
    Point<int16_t> min_point;
    Point<int16_t> max_point;

    auto get_center() -> Point<int16_t> {
        // TODO: Remove these casts.
        return Point<int16_t>{
            .x = static_cast<int16_t>((min_point.x + max_point.x) / 2),
            .y = static_cast<int16_t>((min_point.y + max_point.y) / 2),
            .z = static_cast<int16_t>((min_point.z + max_point.z) / 2),
        };
    }

    auto intersect(Ray& ray) -> bool {
        double distance_x_1 =
            (min_point.x - ray.origin.x) * ray.direction_inverse.x;
        double distance_x_2 =
            (max_point.x - ray.origin.x) * ray.direction_inverse.x;

        double min_distance = std::min(distance_x_1, distance_x_2);
        double max_distance = std::max(distance_x_1, distance_x_2);

        double distance_y_1 =
            (min_point.y - ray.origin.y) * ray.direction_inverse.y;
        double distance_y_2 =
            (max_point.y - ray.origin.y) * ray.direction_inverse.y;

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
    uint8_t red, green, blue;
};

template <int entity_count>
struct Entities {
    AABB aabbs[entity_count];
    Point<int32_t> positions[entity_count];
    Pixel colors[entity_count];  // TODO: Make sprite.
    int32_t last_entity_index = 0;

    using Entity = struct {
        AABB aabb;
        Point<int32_t> position;
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

constexpr int16_t cell_size = 20;
constexpr int32_t view_width = 480;
constexpr int32_t view_height = 320;
constexpr int32_t bin_count_in_view_width = view_width / cell_size;
constexpr int32_t bin_count_in_view_height = view_height * 2 / cell_size;
constexpr int32_t bin_count_in_view_length = view_height / cell_size;

auto index_view_cube(int x, int y, int z) -> int {
    return (x * bin_count_in_view_height * bin_count_in_view_length) +
           // The y must be offset by the number of cells below the view's
           // origin, to prevent negative indexing.
           ((y + bin_count_in_view_height / 2) * bin_count_in_view_length) + z;
}

auto main() -> int {
    constexpr int entity_count = 20;
    auto p_entities = new (std::nothrow) Entities<entity_count>;

    // An AABB's volume is currently hard-coded to 20^3.
    for (int i = 0; i < p_entities->size(); i++) {
        // Place entities randomly throughout a cube which bounds the
        // orthographic view frustrum, assuming the camera is at <0,0,0>.
        int32_t x = rand() % (view_width);
        int32_t y = (rand() % (view_height * 2)) - view_height;
        int32_t z = rand() % (view_height);

        Point<int32_t> new_position = {x, y, z};

        p_entities->insert({
            .aabb = {.min_point = {static_cast<int16_t>(new_position.x),
                                   static_cast<int16_t>(new_position.y),
                                   static_cast<int16_t>(new_position.z)},
                     .max_point = {static_cast<int16_t>(new_position.x + 20),
                                   static_cast<int16_t>(new_position.y + 20),
                                   static_cast<int16_t>(new_position.z + 20)}},
            .position = new_position,
            .color = {255u, 255u, 255u},
        });
    }

    // p_entities is random-access, but the bins they're stored into are not, so
    // we must store a random-access map to the entities' attributes.
    auto p_aabb_index_to_entity_index_map = new (std::nothrow)
        int32_t[bin_count_in_view_width * bin_count_in_view_height *
                bin_count_in_view_length];

    // Track how many entities fit into each bin.
    auto p_aabb_count_in_bin = new (
        std::nothrow) int[bin_count_in_view_width * bin_count_in_view_height *
                          bin_count_in_view_length];

    auto* p_aabb_bins = new (std::nothrow)
        AABB[bin_count_in_view_width * bin_count_in_view_height *
             bin_count_in_view_length];
    int entity_count_currently_in_bin[bin_count_in_view_width *
                                      bin_count_in_view_height *
                                      bin_count_in_view_length];

    // Determine the address offset of each entity AABB bin.
    auto* p_aabb_bin_index_offset = new (
        std::nothrow) int[bin_count_in_view_width * bin_count_in_view_height *
                          bin_count_in_view_length];

    for (int i = 0; i < p_entities->size(); i++) {
        AABB& this_aabb = p_entities->aabbs[i];

        // Get the cells that this AABB fits into.
        int min_x_index = std::max(this_aabb.min_point.x / cell_size, 0);
        int min_y_index = std::max(this_aabb.min_point.y / cell_size, 0);
        int min_z_index = std::max(this_aabb.min_point.z / cell_size, 0);

        int max_x_index = std::min(this_aabb.max_point.x / cell_size,
                                   bin_count_in_view_width);
        int max_y_index = std::min(this_aabb.max_point.y / cell_size,
                                   bin_count_in_view_height);
        int max_z_index = std::min(this_aabb.max_point.z / cell_size,
                                   bin_count_in_view_length);

        // TODO: Test if this entity is inside the view frustrum.

        // Place this AABB into every bin that it spans across.
        for (int x = min_x_index; x <= max_x_index; x += 1) {
            for (int y = min_y_index; y <= max_y_index; y += 1) {
                for (int z = min_z_index; z <= max_z_index; z += 1) {
                    p_aabb_bins[index_view_cube(x, y, z)] = this_aabb;

                    p_aabb_count_in_bin[index_view_cube(x, y, z)] += 1;
                    p_aabb_index_to_entity_index_map[index_view_cube(x, y, z)] =
                        i;
                }
            }
        }
    }

    Point<int16_t> ray_direction = {
        .x = 0,
        .y = -1,
        .z = 1,
    };

    auto p_texture = new (std::nothrow) Pixel[view_height * view_width];
    for (int16_t i = 0; i < view_width; i++) {
        // `j` is a ray's world-position skywards.
        for (int16_t j = 0; j < view_height * 2; j++) {
            Ray this_ray{
                .origin =
                    {
                        .x = i,
                        .y = j,
                        .z = 0,
                    },
                .direction_inverse =
                    {
                        // TODO: This might not work ..
                        .x = 0,
                        .y = 1,  // -1 / 1
                        .z = 1,  //  1 / 1
                    },
            };

            // `k` is a ray's world-position casting forwards.
            // It is 2 bytes because it operates on AABB coordinates, which
            // are 2 bytes.
            for (int16_t k = 0; k < bin_count_in_view_length; k++) {
                int16_t bin_x = static_cast<int16_t>(i / cell_size);
                int16_t bin_y = static_cast<int16_t>((j - k) / cell_size);
                int16_t bin_z = static_cast<int16_t>(k / cell_size);

                if (bin_y < bin_count_in_view_height) {
                    continue;
                }

                Pixel background_color = {0, 0, 0};
                int16_t closest_entity_depth =
                    std::numeric_limits<int16_t>::max();

                int entities_in_this_bin =
                    p_aabb_count_in_bin[index_view_cube(bin_x, bin_y, bin_z)];

                for (int this_bin_entity_index = 0;
                     this_bin_entity_index <
                     p_aabb_count_in_bin[index_view_cube(bin_x, bin_y, bin_z)];
                     this_bin_entity_index++) {
                    AABB& this_aabb =
                        p_aabb_bins[p_aabb_bin_index_offset[index_view_cube(
                                        bin_x, bin_y, bin_z)] +
                                    this_bin_entity_index];

                    // TODO: If this entity has closer depth.
                    // if (this_aabb.min_point.y + (j - this_aabb.min_point.y) >
                    // closest_entity_depth) {

                    // Intersect ray with this aabb.
                    if (this_aabb.intersect(this_ray)) {
                        background_color =
                            p_entities->colors[p_aabb_index_to_entity_index_map
                                                   [index_view_cube(
                                                       bin_x, bin_y, bin_z)]];
                        // TODO: Update `closest_entity_depth`.
                        // TODO: Break ray when a cell has any hits.
                    }
                    // }
                }

                // The camera origin starts at 0, so the current row is
                // subtracted from the texture height.
                p_texture[(view_height - j - k) * view_width + i] =
                    background_color;
            }
        }
    }

    // for (int j = 0; j < 320; j++) {
    //     for (int i = 0; i < 480; i++) {
    //         std::cout << std::to_string(texture[i][j].blue) << ' ';
    //     }
    //     std::cout << '\n';
    // }

    // TODO: Make a trivial pass-through graphics shader pipeline in Vulkan to
    // render texture.
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
