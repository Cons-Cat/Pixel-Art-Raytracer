#include <liblava/lava.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <limits>
#include <string>

// TODO: Automate this in CMake.
#if Release == 1
#define SHADERS_PATH "./res/"
#else
#define SHADERS_PATH "../../res/"
#endif

#ifdef WIN32
#include <windows.h>
auto get_run_path() -> std::string {
    std::array<char, MAX_PATH> result{};
    std::string full_path = std::string(
        result.data(), GetModuleFileName(NULL, result.data(), MAX_PATH));
    return std::string(full_path.substr(0, full_path.find_last_of('\\\\'))) +
           "/";
}
#else
#include <linux/limits.h>
#include <unistd.h>
auto get_run_path() -> std::string {
    std::array<char, PATH_MAX - NAME_MAX> result{};
    ssize_t count =
        readlink("/proc/self/exe", result.data(), PATH_MAX - NAME_MAX);
    std::string full_path = std::string(result.data());
    return std::string(full_path.substr(0, full_path.find_last_of('/'))) + "/";
}
#endif

uint32_t view_width = 480u;
uint32_t view_height = 300u;

struct PointLight {
    alignas(16) std::array<int32_t, 3> position;
};

struct Sprite {
    // All sprites are 20x20.
    int32_t atlas_index;
    // Sprites have an [x,y,z] offset from the origin of an entity.
    int32_t offset_x, offset_y, offset_z;
};

template <typename Pixel>
struct SpriteAtlas {
    static constexpr int32_t sprites_count = 2;
    static constexpr int32_t sprite_width = 20;
    static constexpr int32_t sprite_height = 20;
    Pixel pixels[sprites_count * sprite_width * sprite_height];

    // All sprites are drawn from their top-left, incrementing horizontally
    // across, and then vertically across.
    void make_cube_top(int32_t atlas_index) {
        for (int32_t j = 0; j < sprite_height; j++) {
            for (int32_t i = 0; i < sprite_width; i++) {
                int32_t const pixel_index =
                    (atlas_index * sprite_width * sprite_height) +
                    j * sprite_width + i;
                pixels[pixel_index].normal = {0.f, 1.f, 0.f};
                pixels[pixel_index].palette_index = 30u;
                // Depth increases from 0 to 20 backwards along the top
                // face.
                pixels[pixel_index].depth[1] = j;
            }
        }
    }

    void make_cube_front(int32_t atlas_index) {
        for (int32_t j = 0; j < sprite_height; j++) {
            for (int32_t i = 0; i < sprite_width; i++) {
                int32_t const pixel_index =
                    (atlas_index * sprite_width * sprite_height) +
                    j * sprite_width + i;
                pixels[pixel_index].normal = {0.f, 0.f, 1.f};
                pixels[pixel_index].palette_index = 30u;
                // Depth increases from 0 to 20 downward along the front
                // face.
                pixels[pixel_index].depth[0] = -j;
            }
        }
    }
};

struct Entity {
    // Origin point of this entity.
    int32_t origin_x, origin_y, origin_z;
    int32_t sprites_count = 0;
    Sprite* sprites;

    auto get_origin() -> std::array<int32_t, 3> {
        return {origin_x, origin_y, origin_z};
    }
};

auto initialize_universe() -> Entity* {
    // Initialize cubes.
    Entity* p_cubes = new (std::nothrow) Entity[8];
    for (int32_t i = 0; i < 8; i++) {
        int32_t rand_x = rand() % (480 - 20);
        int32_t rand_y = rand() % (300 - 40);
        p_cubes[i].origin_x = rand_x;
        p_cubes[i].origin_y = rand_y;
        p_cubes[i].origin_z = 0;
        p_cubes[i].sprites_count = 2;
        p_cubes[i].sprites =
            new (std::nothrow) Sprite[p_cubes[i].sprites_count]{
                // Cubes have an origin at their top edge, on their front-left
                // corner.
                Sprite{
                    // Top face of a cube.
                    .atlas_index = 0,
                    .offset_x = 0,
                    .offset_y = -20,
                    .offset_z = 0,
                },
                Sprite{
                    // Front face of a cube.
                    .atlas_index = 1,
                    .offset_x = 0,
                    .offset_y = 0,
                    .offset_z = 0,
                },
            };
    }
    return p_cubes;
}

template <typename Pixel>
auto initialize_sprite_atlas() -> SpriteAtlas<Pixel>* {
    SpriteAtlas<Pixel>* p_sprite_atlas =
        new (std::nothrow) SpriteAtlas<Pixel>();
    for (int32_t j = 0; j < p_sprite_atlas->sprite_height; j++) {
        for (int32_t i = 0; i < p_sprite_atlas->sprite_width; i++) {
            p_sprite_atlas->pixels[i + j * p_sprite_atlas->sprite_width] =
                Pixel{
                    .depth = {0, 0},
                    .palette_index = 0,
                };
        }
    }
    p_sprite_atlas->make_cube_front(0);
    p_sprite_atlas->make_cube_top(1);
    return p_sprite_atlas;
}
