#include <liblava/lava.hpp>

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

constexpr int16_t cell_size = 20;
constexpr int32_t view_width = 480;
constexpr int32_t view_height = 320;
constexpr int32_t cells_count_in_view_width = view_width / cell_size;
constexpr int32_t cells_count_in_view_height = view_height / cell_size;
constexpr int32_t cells_count_in_view_length = view_height / cell_size;

auto main() -> int {
    constexpr int entity_count = 20;
    Entities<entity_count> entities;

    // The AABB volume is currently hard-coded to 20^3.
    for (int i = 0; i < entities.size(); i++) {
        int32_t x = rand() % (view_width - 20);
        int32_t y = rand() % (view_height - 40);
        int32_t z =
            rand() % (view_height - 40 - y);  // This may be slightly too large.

        Point<int32_t> new_position = {x, y, z};

        entities.insert({
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

    int32_t aabb_to_entity_index_map[cells_count_in_view_width]
                                    [cells_count_in_view_height]
                                    [cells_count_in_view_length];

    // Determine how many entities fit into each entity bin.
    int8_t aabb_count_in_bin[cells_count_in_view_width]
                            [cells_count_in_view_height]
                            [cells_count_in_view_length];
    int aabbs_count_in_view = 0;

    for (int i = 0; i < entities.size(); i++) {
        AABB& this_aabb = entities.aabbs[i];

        // TODO: If this entity is inside the view bounds.
        // if ((this_aabb.min_point.x >= 0) && (this_aabb.max_point.x <
        // view_width)
        //     && (this_aabb.min_point.y >= 0) &&
        //     (this_aabb.max_point.y < view_height / 2) &&
        //     (this_aabb.min_point.z >= 0) &&
        //     (this_aabb.max_point.z < view_height / 2)
        // ) {

        // Get the cells that this AABB fits into.
        int min_x_index = this_aabb.min_point.x / cell_size;
        int min_y_index = this_aabb.min_point.y / cell_size;
        int min_z_index = this_aabb.min_point.z / cell_size;
        int max_x_index = this_aabb.max_point.x / cell_size;
        int max_y_index = this_aabb.max_point.y / cell_size;
        int max_z_index = this_aabb.max_point.z / cell_size;

        // Place this AABB into every bin that it spans across.
        for (int x = min_x_index; x <= max_x_index; x++) {
            for (int y = min_x_index; y <= max_y_index; y++) {
                for (int z = min_x_index; z <= max_z_index; z++) {
                    aabb_count_in_bin[x][y][z] += 1;
                    aabbs_count_in_view += 1;
                    aabb_to_entity_index_map[x][y][z] = i;
                }
            }
        }
        // }
    }

    // Determine the address offset of each entity bin.
    int aabb_bin_index_offset[cells_count_in_view_width]
                             [cells_count_in_view_height]
                             [cells_count_in_view_length];
    int aabb_index_offset_accumulator = 0;
    for (int x = 0; x < cells_count_in_view_width; x++) {
        for (int y = 0; y < cells_count_in_view_height; y++) {
            for (int z = 0; z < cells_count_in_view_length; z++) {
                aabb_bin_index_offset[x][y][z] = aabb_index_offset_accumulator *
                                                 static_cast<int>(sizeof(AABB));
                aabb_index_offset_accumulator += aabb_count_in_bin[x][y][z];
            }
        }
    }

    AABB* p_aabb_bins = new (std::nothrow) AABB[aabbs_count_in_view];
    int8_t entity_count_currently_in_bin[cells_count_in_view_width]
                                        [cells_count_in_view_height]
                                        [cells_count_in_view_length];

    for (int i = 0; i < aabbs_count_in_view; i++) {
        AABB& this_aabb = entities.aabbs[i];
        // TODO: Consider entity's dimensions.
        // if ((this_aabb.min_point.x >= 0) &&
        //     (this_aabb.max_point.x < view_width) &&
        //     (this_aabb.min_point.y >= 0) &&
        //     (this_aabb.max_point.y < view_height / 2) &&
        //     (this_aabb.min_point.z >= 0) &&
        //     (this_aabb.max_point.z < view_height / 2)) {

        // Get the cells that this AABB fits into.
        int min_x_index = this_aabb.min_point.x / cell_size;
        int min_y_index = this_aabb.min_point.y / cell_size;
        int min_z_index = this_aabb.min_point.z / cell_size;
        int max_x_index = this_aabb.max_point.x / cell_size;
        int max_y_index = this_aabb.max_point.y / cell_size;
        int max_z_index = this_aabb.max_point.z / cell_size;

        // Place this AABB into every bin that it spans across.
        for (int x = min_x_index; x < max_x_index; x++) {
            for (int y = min_x_index; y < max_y_index; y++) {
                for (int z = min_x_index; z < max_z_index; z++) {
                    // Place this this entity into the bins at an address
                    // relative to the offset of this <x,y,z> pair, plus an
                    // offset which approaches the count of AABBs in that
                    // bin.
                    p_aabb_bins[aabb_bin_index_offset[x][y][z] +
                                entity_count_currently_in_bin[x][y][z]] =
                        this_aabb;
                    entity_count_currently_in_bin[x][y][z] += 1;
                }
            }
        }
        // }
    }

    Point<int16_t> ray_direction = {
        .x = 0,
        .y = -1,
        .z = 1,
    };

    Pixel texture[view_width][view_height];
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
                .direction = ray_direction,
                .direction_inverse =
                    {
                        // TODO: This might not work ..
                        .x = 0,
                        .y = static_cast<int16_t>(1 / ray_direction.y),
                        .z = static_cast<int16_t>(1 / ray_direction.z),
                    },
            };

            // `k` is a ray's world-position casting earthwards.
            // It is 2 bytes because it operates with AABB coordinates, which
            // are 2 bytes.
            for (int16_t k = 0; j - k >= 0; k++) {
                if (j - k < view_height) {
                    continue;
                }
                int16_t cell_x = static_cast<int16_t>(i / cell_size);
                int16_t cell_y = static_cast<int16_t>((j - k) / cell_size);
                int16_t cell_z = static_cast<int16_t>(k / cell_size);

                Pixel color = {0, 0, 0};
                int16_t closest_entity_depth =
                    std::numeric_limits<int16_t>::max();
                for (int ii = 0; ii < aabb_count_in_bin[cell_x][cell_y][cell_z];
                     ii++) {
                    AABB& this_aabb = p_aabb_bins
                        [aabb_bin_index_offset[cell_x][cell_y][cell_z] + ii];

                    // TODO: If this entity has closer depth.
                    // if (this_aabb.min_point.y + (j - this_aabb.min_point.y) >
                    // closest_entity_depth) {

                    // Intersect ray with this aabb.
                    if (this_aabb.intersect(this_ray)) {
                        color =
                            entities
                                .colors[aabb_to_entity_index_map[cell_x][cell_y]
                                                                [cell_z]];
                        // TODO: Update `closest_entity_depth`.
                        // TODO: Break ray when a cell has any hits.
                    }
                    // }
                }

                texture[i][j] = color;
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
