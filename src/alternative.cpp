#include <liblava/lava.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <new>

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
    int8_t red, green, blue;
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
    void insert(Entity entity) {
        aabbs[last_entity_index] = entity.aabb;
        positions[last_entity_index] = entity.position;
        colors[last_entity_index] = entity.color;
        last_entity_index += 1;
    }
    auto size() -> int {
        return entity_count;
    }
};

constexpr int8_t cell_size = 20;
constexpr int32_t view_width = 480;
constexpr int32_t view_height = 320;
constexpr int8_t cells_in_view_width = view_width / cell_size;
constexpr int8_t cells_in_view_height = view_width / cell_size;
constexpr int8_t cells_in_view_length = view_width / cell_size;

auto main() -> int {
    constexpr int entity_count = 128;
    Entities<entity_count> entities;
    for (int i = 0; i < entities.size(); i++) {
        Point<int32_t> new_position = {rand(), rand(), rand()};
        entities.insert({
            .aabb = {.min_point = {static_cast<int16_t>(new_position.x),
                                   static_cast<int16_t>(new_position.y),
                                   static_cast<int16_t>(new_position.z)},
                     .max_point = {static_cast<int16_t>(new_position.x + 20),
                                   static_cast<int16_t>(new_position.y + 20),
                                   static_cast<int16_t>(new_position.z + 20)}},
            .position = new_position,
            .color = {static_cast<int8_t>(255), static_cast<int8_t>(255),
                      static_cast<int8_t>(255)},
        });
    }

    // Determine how many entities fit into each entity bin.
    int8_t entity_count_in_bin[cells_in_view_width][cells_in_view_height]
                              [cells_in_view_length];
    int entities_in_view = 0;
    for (int i = 0; i < entities.size(); i++) {
        AABB& this_aabb = entities.aabbs[i];
        // If this entity is inside the view bounds.
        if (!(this_aabb.min_point.x < 0) ||
            !(this_aabb.max_point.x > view_width) ||
            !(this_aabb.min_point.y < 0) ||
            !(this_aabb.max_point.y > view_height / 2) ||
            !(this_aabb.min_point.z < 0) ||
            !(this_aabb.max_point.z > view_height)) {
            int8_t x = static_cast<int8_t>(this_aabb.min_point.x / cell_size);
            int8_t y =
                static_cast<int8_t>(this_aabb.min_point.y / 2 / cell_size);
            int8_t z =
                static_cast<int8_t>(this_aabb.min_point.z / 2 / cell_size);

            entity_count_in_bin[x][y][z] += 1;
            entities_in_view += 1;
            // TODO: Sort into multiple bins.
        }
    }

    // Determine the address offset of each entity bin.
    int aabb_bin_index_offset[cells_in_view_width][cells_in_view_height]
                             [cells_in_view_length];
    int aabb_offset_accumulator = 0;
    for (int x = 0; x < cells_in_view_width; x++) {
        for (int y = 0; y < cells_in_view_height; y++) {
            for (int z = 0; z < cells_in_view_length; z++) {
                aabb_bin_index_offset[x][y][z] =
                    aabb_offset_accumulator * static_cast<int>(sizeof(AABB));
                aabb_offset_accumulator += entity_count_in_bin[x][y][z];
            }
        }
    }

    AABB* p_aabb_bins = new (std::nothrow) AABB[entities_in_view];
    for (int i = 0; i < entities_in_view; i++) {
        AABB& this_aabb = entities.aabbs[i];
        // TODO: Consider entity's dimensions.
        if (!(this_aabb.min_point.x < 0) ||
            !(this_aabb.max_point.x > view_width) ||
            !(this_aabb.min_point.y < 0) ||
            !(this_aabb.max_point.y > view_height / 2) ||
            !(this_aabb.min_point.z < 0) ||
            !(this_aabb.max_point.z > view_height)) {
            int8_t x = static_cast<int8_t>(this_aabb.min_point.x / cell_size);
            int8_t y =
                static_cast<int8_t>(this_aabb.min_point.y / 2 / cell_size);
            int8_t z =
                static_cast<int8_t>(this_aabb.min_point.z / 2 / cell_size);

            // Subtract entity_bins_counts, because it is used as an address
            // offset.
            entity_count_in_bin[x][y][z] -= 1;
            // Place this this entity into the bins at an address relative to
            // the offset of this <x,y,z> pair, plus an offset which approaches
            // `0`.
            p_aabb_bins[aabb_bin_index_offset[x][y][z] +
                        entity_count_in_bin[x][y][z]] = this_aabb;
        }
    }

    Point<int16_t> ray_direction = {
        .x = 0,
        .y = -1,
        .z = -1,
    };

    Pixel texture[view_width][view_height];
    for (int16_t i = 0; i < view_width; i++) {
        for (int16_t j = 1; j < view_height; j++) {
            Ray this_ray{
                .origin =
                    {
                        .x = i,
                        .y = view_height,
                        .z = 0,
                    },
                .direction = ray_direction,
                // `.direction ./= 1`
                .direction_inverse =
                    {
                        .x = 0,
                        .y = -1,
                        .z = -1,
                    },
            };
            for (int16_t k = 0; k < j; k++) {
                Pixel color;
                int16_t closest_entity_depth =
                    std::numeric_limits<int16_t>::max();
                for (int ii = 0; ii < entity_count_in_bin[i][j][k]; ii++) {
                    AABB& this_aabb =
                        p_aabb_bins[aabb_bin_index_offset[i][j][k] + ii];

                    // If this entity has closer depth.
                    if (this_aabb.min_point.y + (j - this_aabb.min_point.y) >
                        closest_entity_depth) {
                        // Intersect ray with this aabb.
                        if (this_aabb.intersect(this_ray)) {
                            // TODO: Index into the corresponding entity.
                            color = {1, 1, 1};
                        }
                    }
                }
            }
        }
    }

    // TODO: Copy entity color to pixel in frame texture.

    // TODO: Make a trivial pass-through graphics shader pipeline in Vulkan to
    // render texture.
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
