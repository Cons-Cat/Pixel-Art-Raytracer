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

#include "liblava/resource/format.hpp"
#include "vulkan/vulkan_core.h"

// TODO: Automate this in CMake.
#if Release == 1
#define SHADERS_PATH "./res/"
#else
#define SHADERS_PATH "../../res/"
#endif

#ifdef WIN32
#include <windows.h>
inline auto get_run_path() -> std::string {
    std::array<char, MAX_PATH> result{};
    std::string full_path = std::string(
        result.data(), GetModuleFileName(NULL, result.data(), MAX_PATH));
    return std::string(full_path.substr(0, full_path.find_last_of('\\\\'))) +
           "/";
}
#else
#include <linux/limits.h>
#include <unistd.h>
inline auto get_run_path() -> std::string {
    std::array<char, PATH_MAX - NAME_MAX> result{};
    ssize_t count =
        readlink("/proc/self/exe", result.data(), PATH_MAX - NAME_MAX);
    std::string full_path = std::string(result.data());
    return std::string(full_path.substr(0, full_path.find_last_of('/'))) + "/";
}
#endif

uint32_t view_width = 480u;
uint32_t view_height = 300u;

// TODO: Figure out struct packing and half precision floats across C++ and
// Slang.
struct Pixel {
    // <X, Y, Z>
    alignas(16) std::array<float, 3> normal;
    // <Y, Z>
    alignas(8) std::array<int32_t, 2> depth;
    alignas(4) uint32_t palette_index;
};

struct PixelBucket {
    // Size is a positive value, but it is signed to enable optimizations.
    alignas(4) int32_t size = 0;
    Pixel pixels[8];

    // TODO: r-value reference?
    void push(Pixel pixel) {
        // TODO: Make this a ring buffer instead of saturated buffer.
        if (this->size < 8) [[likely]] {
            // Don't push pixels beyond a clipping plane.
            // if (pixel.depth > -sqrt(300 * 300 + 300 * 300)) [[likely]] {
            this->pixels[this->size] = pixel;
            this->size++;
            // }
        }
    }
};

struct Cell {
    // There are 4x4 containers to display in the cell, and each of them can
    // hold up to 8 pixels. This is height x width.
    PixelBucket pixel_buckets[4][4];
};

struct PointLight {
    alignas(16) std::array<int32_t, 3> position;
};

struct Sprite {
    // All sprites are 20x20.
    int32_t atlas_index;
    // Sprites have an [x,y,z] offset from the origin of an entity.
    int32_t offset_x, offset_y, offset_z;
};

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

// Pixel buffer data structure.
struct GpuPixelBuffer {
    Cell cells[75][120];
    static constexpr int32_t point_light_count = 1;
    PointLight point_lights[point_light_count];

    void clear() {
        for (int32_t cell_y = 0; cell_y < 75; cell_y++) {
            for (int32_t cell_x = 0; cell_x < 120; cell_x++) {
                for (int32_t j = 0; j < 4; j++) {
                    for (int32_t i = 0; i < 4; i++) {
                        this->cells[cell_y][cell_x].pixel_buckets[j][i].size =
                            0;
                        for (int32_t k = 0; k < 8; k++) {
                            Pixel& current_pixel = this->cells[cell_y][cell_x]
                                                       .pixel_buckets[j][i]
                                                       .pixels[k];
                            current_pixel.palette_index = 0;
                            current_pixel.depth = {0, 0};
                        }
                    }
                }
            }
        }
    }

    void draw_sprite(SpriteAtlas const* p_sprite_atlas, int32_t world_x,
                     int32_t world_y, int32_t world_z, int32_t atlas_index) {
        for (int32_t j = 0; j < p_sprite_atlas->sprite_height; j++) {
            for (int32_t i = 0; i < p_sprite_atlas->sprite_width; i++) {
                int32_t view_x = world_x + i;
                int32_t view_y = world_y + j - world_z;
                if (view_x < 0 || view_x >= view_width || view_y < 0 ||
                    view_y >= view_height) {
                    // Do not push sprites out of bounds.
                    continue;
                }
                int32_t cell_x = view_x / 4;
                int32_t cell_y = view_y / 4;
                Cell& current_cell = this->cells[cell_y][cell_x];
                int32_t const current_pixel_index =
                    (atlas_index * p_sprite_atlas->sprite_width *
                     p_sprite_atlas->sprite_height) +
                    j * p_sprite_atlas->sprite_width + i;

                Pixel const& current_pixel =
                    p_sprite_atlas->pixels[current_pixel_index];

                PixelBucket& current_pixel_bucket =
                    current_cell.pixel_buckets[view_y - cell_y * 4]
                                              [view_x - cell_x * 4];
                current_pixel_bucket.push(Pixel{
                    .normal = current_pixel.normal,
                    .depth =
                        {
                            // Y depth offset (skyward).
                            current_pixel.depth[0] - world_y,
                            // Z depth offset (forward).
                            current_pixel.depth[1] - world_z,
                        },
                    .palette_index = current_pixel.palette_index,
                });
            }
        }
    }
};

// Program.
auto main() -> int {
    std::cout << "Hello, user!\n";

    SpriteAtlas* p_sprite_atlas = new (std::nothrow) SpriteAtlas();

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

    GpuPixelBuffer* p_pixel_buffer_data = new (std::nothrow) GpuPixelBuffer();
    p_pixel_buffer_data->clear();
    for (int i = 0; i < p_pixel_buffer_data->point_light_count; i++) {
        p_pixel_buffer_data->point_lights[i].position = {0, 10, 10};
    }

    // Initialize cubes.
    Entity cubes[8];
    for (int32_t i = 0; i < 8; i++) {
        int32_t rand_x = rand() % (480 - 20);
        int32_t rand_y = rand() % (300 - 40);
        cubes[i].origin_x = rand_x;
        cubes[i].origin_y = rand_y;
        cubes[i].origin_z = 0;
        cubes[i].sprites_count = 2;
        cubes[i].sprites = new (std::nothrow) Sprite[cubes[i].sprites_count]{
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

    lava::frame_config config;
    config.param.extensions.insert(config.param.extensions.end(),
                                   {
                                       "VK_KHR_get_physical_device_properties2",
                                   });
    lava::app app(config);
    app.config = {
        .surface =
            lava::surface_format_request{
                .formats = {VK_FORMAT_R8G8B8A8_UNORM},
                .color_space = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
            },
    };
    VkPhysicalDevice8BitStorageFeatures features_2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES,
        .pNext = nullptr,
        .storageBuffer8BitAccess = VK_TRUE,
        .uniformAndStorageBuffer8BitAccess = VK_TRUE,
    };
    VkPhysicalDeviceShaderFloat16Int8FeaturesKHR features_1 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
        .pNext = &features_2,
        .shaderInt8 = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 const features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features_1,
        .features =
            {
                .shaderFloat64 = VK_TRUE,
                .shaderInt64 = VK_TRUE,
            },
    };
    app.manager.on_create_param = [&](lava::device::create_param& param) {
        param.next = &features;
        param.extensions.insert(param.extensions.end(),
                                {
                                    "VK_KHR_shader_float16_int8",
                                    "VK_KHR_storage_buffer_storage_class",
                                    "VK_KHR_8bit_storage",
                                });
    };
    app.setup();
    app.window.set_size(view_width, view_height);

    uint32_t max_workgroups =
        app.device->get_properties().limits.maxComputeWorkGroupInvocations;
    // TODO: I should hard-code this for now.
    // VkCmdDispatch takes unsigned integers.
    uint32_t const workgroup_width = view_width / 4u;
    uint32_t const workgroup_height = view_height / 4u;
    uint32_t const workgroup_depth = 4u;

    lava::graphics_pipeline::ptr raster_pipeline;
    lava::pipeline_layout::ptr raster_pipeline_layout;

    lava::compute_pipeline::ptr compute_pipeline;
    lava::pipeline_layout::ptr compute_pipeline_layout;

    lava::compute_pipeline::ptr depth_pipeline;
    lava::pipeline_layout::ptr depth_pipeline_layout;

    lava::compute_pipeline::ptr normals_pipeline;
    lava::pipeline_layout::ptr normals_pipeline_layout;

    lava::pipeline::shader_stage::ptr shader_stage;

    VkCommandPool p_cmd_pool;

    lava::descriptor::ptr shared_descriptor_layout;
    lava::descriptor::pool::ptr descriptor_pool;
    VkDescriptorSet p_shared_descriptor_set_image = nullptr;
    VkDescriptorSet p_shared_descriptor_set_pixels = nullptr;

    // Our Oklab color conversions already give us sRGB colors, so we want to
    // sample from it linearly.
    lava::image storage_image(VK_FORMAT_R8G8B8A8_UNORM);
    // This does not have the sampled bit.
    storage_image.set_usage(VK_IMAGE_USAGE_STORAGE_BIT);

    // Staging buffer is host-writable.
    lava::buffer::ptr pixel_buffer_staging;
    // Device buffer is device-visible.
    lava::buffer::ptr pixel_buffer_device;

    auto fn_cmd_transfer_pixel_buffer_memory = [&](VkCommandBuffer p_cmd_buf) {
        VkBufferCopy copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = sizeof(GpuPixelBuffer),
        };
        vkCmdCopyBuffer(p_cmd_buf, pixel_buffer_staging->get(),
                        pixel_buffer_device->get(), 1, &copy);
    };

    auto fn_cmd_record_storage_image_transition =
        [&](VkCommandBuffer p_cmd_buf) {
            lava::set_image_layout(app.device, p_cmd_buf, storage_image.get(),
                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_GENERAL);
        };

    app.on_create = [&]() {
        descriptor_pool = lava::make_descriptor_pool();
        descriptor_pool->create(app.device,
                                {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                 {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}},
                                2);

        app.device->vkCreateCommandPool(app.device->graphics_queue().family,
                                        &p_cmd_pool);

        // Making image buffer.
        shared_descriptor_layout = lava::make_descriptor();
        shared_descriptor_layout->add_binding(
            0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
        shared_descriptor_layout->add_binding(
            1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
        shared_descriptor_layout->create(app.device);

        storage_image.create(app.device, {view_width, view_height});

        // Making pixel buffer.
        pixel_buffer_staging = lava::make_buffer();
        pixel_buffer_staging->create(
            app.device, p_pixel_buffer_data, sizeof(GpuPixelBuffer),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, VMA_MEMORY_USAGE_CPU_ONLY);

        pixel_buffer_device = lava::make_buffer();
        pixel_buffer_device->create(app.device, p_pixel_buffer_data,
                                    sizeof(GpuPixelBuffer),
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    true, VMA_MEMORY_USAGE_GPU_ONLY);

        auto fn_one_time_cmd = [&](VkCommandBuffer p_cmd_buf) {
            fn_cmd_transfer_pixel_buffer_memory(p_cmd_buf);
            fn_cmd_record_storage_image_transition(p_cmd_buf);
        };

        lava::one_time_command_buffer(app.device, p_cmd_pool,
                                      app.device->get_graphics_queue(),
                                      fn_one_time_cmd);

        VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = storage_image.get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = storage_image.get_format(),
            .components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                           VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A},
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };

        VkImageView p_view;
        vkCreateImageView(app.device->get(), &view_info, nullptr, &p_view);

        VkDescriptorImageInfo descriptor_image_info = {
            .imageView = p_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        // Create the descriptor sets.
        p_shared_descriptor_set_image =
            shared_descriptor_layout->allocate(descriptor_pool->get());
        VkWriteDescriptorSet const write_desc_storage_image{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = p_shared_descriptor_set_image,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descriptor_image_info,
        };
        VkWriteDescriptorSet const write_desc_storage_pixels{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = p_shared_descriptor_set_image,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = pixel_buffer_device->get_descriptor_info(),
        };

        app.device->vkUpdateDescriptorSets({
            write_desc_storage_image,
            write_desc_storage_pixels,
        });

        // Create compute pipeline.
        compute_pipeline = lava::make_compute_pipeline(app.device);
        compute_pipeline_layout = lava::make_pipeline_layout();
        compute_pipeline_layout->add_descriptor(shared_descriptor_layout);
        compute_pipeline_layout->create(app.device);
        compute_pipeline->set_layout(compute_pipeline_layout);
        compute_pipeline->set_shader_stage(
            lava::file_data(get_run_path() + SHADERS_PATH + "color.spv"),
            VK_SHADER_STAGE_COMPUTE_BIT);
        compute_pipeline->create();

        // Create depth pipeline.
        depth_pipeline = lava::make_compute_pipeline(app.device);
        depth_pipeline_layout = lava::make_pipeline_layout();
        depth_pipeline_layout->add_descriptor(shared_descriptor_layout);
        depth_pipeline_layout->create(app.device);
        depth_pipeline->set_layout(depth_pipeline_layout);
        depth_pipeline->set_shader_stage(
            lava::file_data(get_run_path() + SHADERS_PATH + "depth.spv"),
            VK_SHADER_STAGE_COMPUTE_BIT);
        depth_pipeline->create();

        // Create normals pipeline.
        normals_pipeline = lava::make_compute_pipeline(app.device);
        normals_pipeline_layout = lava::make_pipeline_layout();
        normals_pipeline_layout->add_descriptor(shared_descriptor_layout);
        normals_pipeline_layout->create(app.device);
        normals_pipeline->set_layout(normals_pipeline_layout);
        normals_pipeline->set_shader_stage(
            lava::file_data(get_run_path() + SHADERS_PATH + "normals.spv"),
            VK_SHADER_STAGE_COMPUTE_BIT);
        normals_pipeline->create();

        // Make raster pipeline.
        raster_pipeline = lava::make_graphics_pipeline(app.device);
        raster_pipeline->add_shader(
            lava::file_data(get_run_path() + SHADERS_PATH + "vertex.spv"),
            VK_SHADER_STAGE_VERTEX_BIT);
        raster_pipeline->add_shader(
            lava::file_data(get_run_path() + SHADERS_PATH + "fragment.spv"),
            VK_SHADER_STAGE_FRAGMENT_BIT);
        raster_pipeline->add_color_blend_attachment();
        raster_pipeline->set_rasterization_cull_mode(VK_CULL_MODE_FRONT_BIT);
        raster_pipeline->set_rasterization_front_face(
            VK_FRONT_FACE_COUNTER_CLOCKWISE);
        raster_pipeline_layout = lava::make_pipeline_layout();
        raster_pipeline_layout->add_descriptor(shared_descriptor_layout);
        raster_pipeline_layout->create(app.device);
        raster_pipeline->set_layout(raster_pipeline_layout);
        raster_pipeline->set_auto_size(true);
        lava::render_pass::ptr render_pass = app.shading.get_pass();
        raster_pipeline->create(render_pass->get());
        render_pass->add_front(raster_pipeline);

        // Hard-code draw of three verts.
        raster_pipeline->on_process = [&](VkCommandBuffer p_cmd_buf) {
            raster_pipeline_layout->bind(p_cmd_buf,
                                         p_shared_descriptor_set_image);
            app.device->call().vkCmdDraw(p_cmd_buf, 3, 1, 0, 0);
        };
        return true;
    };

    app.on_destroy = [&]() {
        // TODO: Clean more memory.
        compute_pipeline->destroy();
        compute_pipeline_layout->destroy();
        raster_pipeline->destroy();
        raster_pipeline_layout->destroy();
    };

    enum RenderMode
    {
        COLOR = 0,
        DEPTH,
        NORMALS,
    };
    RenderMode render_mode = COLOR;
    app.on_process = [&](VkCommandBuffer p_cmd_buf, lava::index) {
        // A command buffer is automatically recording.

        if (render_mode == COLOR) {
            compute_pipeline->bind(p_cmd_buf);
            compute_pipeline_layout->bind_descriptor_set(
                p_cmd_buf, p_shared_descriptor_set_image, 0, {},
                VK_PIPELINE_BIND_POINT_COMPUTE);
        } else if (render_mode == DEPTH) {
            depth_pipeline->bind(p_cmd_buf);
            depth_pipeline_layout->bind_descriptor_set(
                p_cmd_buf, p_shared_descriptor_set_image, 0, {},
                VK_PIPELINE_BIND_POINT_COMPUTE);
        } else if (render_mode == NORMALS) {
            normals_pipeline->bind(p_cmd_buf);
            normals_pipeline_layout->bind_descriptor_set(
                p_cmd_buf, p_shared_descriptor_set_image, 0, {},
                VK_PIPELINE_BIND_POINT_COMPUTE);
        }

        vkCmdDispatch(p_cmd_buf, workgroup_width, workgroup_height,
                      workgroup_depth);

        // Wait on the compute shader to finish.
        VkImageMemoryBarrier image_memory_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = storage_image.get(),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(p_cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &image_memory_barrier);
    };

    bool u, d, l, r, zu, zd = false;
    app.input.key.listeners.add([&](lava::key_event::ref event) {
        if (event.pressed(lava::key::down)) {
            d = true;
        }
        if (event.released(lava::key::down)) {
            d = false;
        }
        if (event.pressed(lava::key::up)) {
            u = true;
        }
        if (event.released(lava::key::up)) {
            u = false;
        }
        if (event.pressed(lava::key::left)) {
            l = true;
        }
        if (event.released(lava::key::left)) {
            l = false;
        }
        if (event.pressed(lava::key::right)) {
            r = true;
        }
        if (event.released(lava::key::right)) {
            r = false;
        }
        if (event.pressed(lava::key::page_up)) {
            zu = true;
        }
        if (event.released(lava::key::page_up)) {
            zu = false;
        }
        if (event.pressed(lava::key::page_down)) {
            zd = true;
        }
        if (event.released(lava::key::page_down)) {
            zd = false;
        }
        if (event.pressed(lava::key::_1)) {
            render_mode = COLOR;
        } else if (event.pressed(lava::key::_2)) {
            render_mode = DEPTH;
        } else if (event.pressed(lava::key::_3)) {
            render_mode = NORMALS;
        }
        return true;
    });

    app.on_update = [&](lava::delta) {
        Entity& e = cubes[1];
        if (r) {
            e.origin_x += 1;
        }
        if (l) {
            e.origin_x -= 1;
        }
        if (u) {
            e.origin_y -= 1;
        }
        if (d) {
            e.origin_y += 1;
        }
        if (zu) {
            e.origin_z += 1;
        }
        if (zd) {
            e.origin_z -= 1;
        }

        p_pixel_buffer_data->clear();
        for (int32_t i = 0; i < 8; i++) {
            Entity const& current_entity = cubes[i];
            for (int32_t j = 0; j < current_entity.sprites_count; j++) {
                Sprite const& current_sprite = current_entity.sprites[j];
                p_pixel_buffer_data->draw_sprite(
                    p_sprite_atlas,
                    current_entity.origin_x + current_sprite.offset_x,
                    current_entity.origin_y + current_sprite.offset_y,
                    current_entity.origin_z + current_sprite.offset_z, j);
            }
        }
        p_pixel_buffer_data->point_lights[0].position = cubes[1].get_origin();

        void* p_data;
        vkMapMemory(app.device->get(),
                    pixel_buffer_staging->get_device_memory(), 0,
                    pixel_buffer_staging->get_size(), 0, &p_data);
        memcpy(p_data, p_pixel_buffer_data, sizeof(GpuPixelBuffer));
        vkUnmapMemory(app.device->get(),
                      pixel_buffer_staging->get_device_memory());
        auto fn_one_time_cmd = [&](VkCommandBuffer p_cmd_buf) {
            fn_cmd_transfer_pixel_buffer_memory(p_cmd_buf);
            fn_cmd_record_storage_image_transition(p_cmd_buf);
        };
        lava::one_time_command_buffer(app.device, p_cmd_pool,
                                      app.device->get_graphics_queue(),
                                      fn_one_time_cmd);
        return true;
    };

    return app.run();
}
