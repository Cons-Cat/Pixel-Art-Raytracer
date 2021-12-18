#include <liblava/lava.hpp>

struct Entity {
    std::array<int32_t, 3> position;
    std::array<float, 3> color_oklab;
};

// This occupies exactly three cache-lines on x64.
struct Bounds {
    int16_t x, y, z, width, height, length;
};

struct Point {
    int16_t x, y, z;
};

auto get_min_point(Point p1, Point p2) -> Point {
    return Point{
        std::min(p1.x, p2.x),
        std::min(p1.y, p2.y),
        std::min(p1.z, p2.z),
    };
}

auto get_max_point(Point p1, Point p2) -> Point {
    return Point{
        std::max(p1.x, p2.x),
        std::max(p1.y, p2.y),
        std::max(p1.z, p2.z),
    };
}

struct alignas(16) AABB {
    Point min_point;
    Point max_point;
    // int32_t const padding = 0;  // This pads out an AABB to 16 bytes.

    // AABB() = default;

    auto get_center() -> Point {
        // TODO: Remove these casts.
        return Point{
            .x = static_cast<int16_t>((min_point.x + max_point.x) / 2),
            .y = static_cast<int16_t>((min_point.y + max_point.y) / 2),
            .z = static_cast<int16_t>((min_point.z + max_point.z) / 2),
        };
    }

    void expand_to_include_point(Point const& point) {
        this->min_point = get_min_point(this->min_point, point);
        this->max_point = get_max_point(this->max_point, point);
    }

    void expand_to_include_box(AABB const& box) {
        this->min_point = get_min_point(this->min_point, box.min_point);
        this->max_point = get_max_point(this->max_point, box.max_point);
    }
};
static_assert(sizeof(AABB) == 16);

auto make_aabb_from_entity(Entity const& entity) -> AABB {
    return AABB{
        .min_point =
            {
                static_cast<int16_t>(entity.position[0]),
                static_cast<int16_t>(entity.position[1]),
                static_cast<int16_t>(entity.position[2]),
            },
        .max_point =
            {
                static_cast<int16_t>(entity.position[0] + 20),
                static_cast<int16_t>(entity.position[1] + 20),
                static_cast<int16_t>(entity.position[2] + 20),
            },
    };
}

struct TreeNode {
    AABB box;
    uint32_t start_index;
    uint32_t right_node_offset;
    uint32_t primitives_count;

    auto is_leaf() -> bool {
        return right_node_offset == 0;
    }
};

struct BVH {
    std::vector<TreeNode> nodes;
    Entity const* entities;
    uint32_t const entity_count;

    BVH(std::vector<TreeNode>&& in_nodes, std::vector<Entity> in_primitives)
        : nodes(in_nodes), entities(in_entities){};
};

struct BuildEntry {
    uint32_t parent_index;
    uint32_t start_index;
    uint32_t end_index;  // TODO: This should only ever be 1.
};

struct BuildStack {
    static constexpr uint32_t max_size = 128;
    BuildEntry entries[max_size];
    uint32_t stack_ptr = 0;

    auto pop() -> BuildEntry {
        stack_ptr--;
        return entries[stack_ptr];
    }

    void push(BuildEntry const& entry) {
        entries[stack_ptr] = entry;
        stack_ptr++;
    }

    auto operator[](uint32_t index) -> BuildEntry& {
        return entries[index];
    }
};

auto build_bvh(std::vector<Entity> primitives) -> BVH {
    BuildStack todo;

    const uint32_t untouched = 0xffffffff;
    const uint32_t touched_twice = 0xfffffffd;
    uint32_t node_count = 0;

    BuildEntry root{
        .parent_index = 0xfffffffc,
        .start_index = 0,
        .end_index = static_cast<uint32_t>(primitives.size()),
    };

    todo.push(root);

    TreeNode node{};
    std::vector<TreeNode> build_nodes;
    build_nodes.reserve(primitives.size() * 2);

    while (todo.stack_ptr > 0) {
        auto bnode = todo.pop();

        uint32_t start_index = bnode.start_index;
        uint32_t end_index = bnode.end_index;
        uint32_t primitive_count = end_index - start_index;

        node_count++;
        node.start_index = start_index;
        node.primitives_count = primitive_count;
        node.right_node_offset = untouched;

        // Calculate the bounding box for this node

        auto bb = make_aabb_from_entity(primitives[start_index]);
        auto bc = AABB{
            .min_point = bb.get_center(),
            .max_point = bb.get_center(),
        };

        for (uint32_t p = start_index + 1; p < end_index; ++p) {
            auto box = make_aabb_from_entity(primitives[p]);
            bb.expand_to_include_box(box);
            bc.expand_to_include_point(box.get_center());
        }

        node.box = bb;

        if (primitive_count <= 1) {
            node.right_node_offset = 0;
        }

        build_nodes.push_back(node);

        if (bnode.parent_index != 0xfffffffc) {
            build_nodes[bnode.parent_index].right_node_offset--;
            if (build_nodes[bnode.parent_index].right_node_offset ==
                touched_twice) {
                build_nodes[bnode.parent_index].right_node_offset =
                    node_count - 1 - bnode.parent_index;
            }
        }
    }

    std::vector<TreeNode> nodes;
    nodes.resize(node_count);

    for (int n = 0; n < node_count; n++) {
        nodes[n] = build_nodes[n];
    }

    return BVH(std::move(nodes), primitives);
}

auto main() -> int {
    std::vector<Entity> entities;
    BVH bvh = build_bvh(entities);
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
