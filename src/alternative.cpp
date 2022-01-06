#include <liblava/lava.hpp>

#include <iostream>
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

struct Entity {
    Point<int32_t> position;
    Pixel color;  // TODO: Make sprite.
};

auto make_aabb_from_entity(Entity const& entity) -> AABB {
    return AABB{
        .min_point =
            {
                static_cast<int16_t>(entity.position.x),
                static_cast<int16_t>(entity.position.y),
                static_cast<int16_t>(entity.position.z),
            },
        .max_point =
            {
                // TODO: Make dimensions variable.
                static_cast<int16_t>(entity.position.x + 20),
                static_cast<int16_t>(entity.position.y + 20),
                static_cast<int16_t>(entity.position.z + 20),
            },
    };
}

constexpr int8_t cell_size = 20;
constexpr int32_t view_width = 480;
constexpr int32_t view_height = 320;
constexpr int8_t cells_in_view_width = view_width / cell_size;
constexpr int8_t cells_in_view_height = view_width / cell_size;
constexpr int8_t cells_in_view_length = view_width / cell_size;

auto main() -> int {
    std::vector<Entity> entities;
    entities.reserve(sizeof(Entity) * 128);
    for (int i = 0; i < entities.capacity(); i++) {
        entities.emplace_back(std::array<int32_t, 3>{rand(), rand(), rand()});
    }

    // Determine how many entities fit into each entity bin.
    int8_t entity_count_in_bin[cells_in_view_width][cells_in_view_height]
                              [cells_in_view_length];
    int entities_in_view = 0;
    for (int i = 0; i < entities.size(); i++) {
        Entity& entity = entities[i];
        // If this entity is inside the view bounds.
        // TODO: Consider entity's dimensions.
        if (!(entity.position.x < 0) || !(entity.position.x > view_width) ||
            !(entity.position.y < 0) ||
            !(entity.position.y > view_height / 2) ||
            !(entity.position.z < 0) || !(entity.position.z > view_height)) {
            int8_t x = static_cast<int8_t>(entity.position.x / cell_size);
            int8_t y = static_cast<int8_t>(entity.position.y / 2 / cell_size);
            int8_t z = static_cast<int8_t>(entity.position.z / 2 / cell_size);
            entity_count_in_bin[x][y][z] += 1;
            entities_in_view += 1;
        }
    }

    // Determine the address offset of each entity bin.
    int entity_bin_index_offset[cells_in_view_width][cells_in_view_height]
                               [cells_in_view_length];
    int entity_offset_accumulator = 0;
    for (int x = 0; x < cells_in_view_width; x++) {
        for (int y = 0; y < cells_in_view_height; y++) {
            for (int z = 0; z < cells_in_view_length; z++) {
                entity_bin_index_offset[x][y][z] =
                    entity_offset_accumulator *
                    static_cast<int>(sizeof(Entity));
                entity_offset_accumulator += entity_count_in_bin[x][y][z];
            }
        }
    }

    Entity* p_entity_bins = new (std::nothrow) Entity[entities_in_view];
    for (int i = 0; i < entities_in_view; i++) {
        Entity& entity = entities[i];
        // TODO: Consider entity's dimensions.
        if (!(entity.position.x < 0) || !(entity.position.x > view_width) ||
            !(entity.position.y < 0) ||
            !(entity.position.y > view_height / 2) ||
            !(entity.position.z < 0) || !(entity.position.z > view_height)) {
            int8_t x = static_cast<int8_t>(entity.position.x / cell_size);
            int8_t y = static_cast<int8_t>(entity.position.y / 2 / cell_size);
            int8_t z = static_cast<int8_t>(entity.position.z / 2 / cell_size);

            // Subtract entity_bins_counts, because it is used as an address
            // offset.
            entity_count_in_bin[x][y][z] -= 1;
            // Place this current entity into the bins at an address relative to
            // the offset of this <x,y,z> pair, plus an offset which approaches
            // `0`.
            p_entity_bins[entity_bin_index_offset[x][y][z] +
                          entity_count_in_bin[x][y][z]] = entities[i];
        }
    }

    // TODO: Make AABBs from entities.

    // TODO: Fit AABBs into grid bins.

    // TODO: Trace 1 ray per pixel through spatial hash.
    Point<int16_t> ray_direction = {
        .x = 0,
        .y = -1,
        .z = -1,
    };

    Pixel texture[view_width][view_height];
    for (int16_t i = 0; i < view_width; i++) {
        for (int16_t j = 1; j < view_height; j++) {
            Ray ray{
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
                for (int ii = 0; ii < entity_count_in_bin[i][j][k]; ii++) {
                    Entity& entity =
                        p_entity_bins[entity_bin_index_offset[i][j][k] + ii];
                    // Intersect ray with this entity.
                }
            }
next_pixel:
            continue;
        }
    }

    // TODO: Test intersection with ray for entities in a bin.

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
