#include <liblava/lava.hpp>

#include <array>

struct Entity {
    std::array<int32_t, 3> position;
    std::array<float, 3> color_oklab;
};

struct AABB {
    AABB* child_left;
    AABB* child_right;
};

struct BVH {};

auto main() -> int {
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
