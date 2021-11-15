#include <liblava/lava.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

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

constexpr uint32_t view_width = 480u;
constexpr uint32_t view_height = 300u;

struct Entity {
    uint32_t tex_x, tex_y, tex_w, tex_h;
    uint32_t x, y, z;
};

struct Pixel {
    uint32_t palette_index;
    uint32_t depth;
};

struct SpriteAtlas {
    static constexpr uint32_t atlas_width = 20u;
    static constexpr uint32_t atlas_height = 40u;
    Pixel pixels[atlas_height][atlas_width];

    void make_cube(int x, int y) {
        uint32_t sprite_width = 20u;
        uint32_t sprite_height = 40u;
        for (int32_t j = 0; j < sprite_height; j++) {
            for (int32_t i = 0; i < sprite_width; i++) {
                pixels[y + j][x + i].palette_index = 0;  // Blank color.
                if (j < sprite_height / 2) {
                    // Top face.
                    pixels[y + j][x + i].palette_index = 31;
                    pixels[y + j][x + i].depth = sprite_height / 2 - j;
                } else {
                    // Front face.
                    pixels[y + j][x + i].palette_index = 30;
                    pixels[y + j][x + i].depth = 0;
                }
            }
        }
    }
};

struct PixelArray {
    uint32_t size;
    Pixel pixels[8];

    // TODO: R-value reference?
    void push(Pixel pixel) {
        this->pixels[size] = pixel;
        this->size++;
    }
};

struct Cell {
    // There are 4x4 containers to display in the cell, and each of them can
    // hold up to 8 pixels. This is height x width.
    PixelArray pixel_buckets[4][4];
};

// Pixel buffer data structure.
struct GpuPixelBuffer {
    Cell cells[75][120];

    void clear() {
        for (uint32_t cell_y = 0; cell_y < 75; cell_y++) {
            for (uint32_t cell_x = 0; cell_x < 120; cell_x++) {
                for (uint32_t j = 0; j < 4; j++) {
                    for (uint32_t i = 0; i < 4; i++) {
                        this->cells[cell_y][cell_x].pixel_buckets[j][i].size =
                            0;
                        for (uint32_t k = 0; k < 8; k++) {
                            Pixel& current_pixel = this->cells[cell_y][cell_x]
                                                       .pixel_buckets[j][i]
                                                       .pixels[k];
                            current_pixel.palette_index = 0;  // Blank color.
                            current_pixel.depth = 255;        // Maximum depth.
                        }
                    }
                }
            }
        }
    }

    // TODO: Should there be an array abstraction?
    void render_entities(Entity* p_entities, uint32_t entity_count,
                         SpriteAtlas* p_sprite_atlas) {
        for (uint32_t cube = 0; cube < entity_count; cube++) {
            Entity& current_entity = p_entities[cube];

            for (uint32_t j = 0; j < current_entity.tex_h; j++) {
                for (uint32_t i = 0; i < current_entity.tex_w; i++) {
                    // TODO: Extract current pixel lookup to function.
                    uint32_t world_x = current_entity.x + i;
                    uint32_t world_y = current_entity.y + j;
                    uint32_t cell_x = world_x / 4;
                    uint32_t cell_y = world_y / 4;
                    Cell& current_cell = this->cells[cell_y][cell_x];
                    PixelArray& current_pixel_bucket =
                        current_cell.pixel_buckets[world_y - cell_y * 4]
                                                  [world_x - cell_x * 4];
                    Pixel& current_pixel =
                        p_sprite_atlas->pixels[current_entity.tex_y + j]
                                              [current_entity.tex_x + i];
                    current_pixel_bucket.push(Pixel{
                        .palette_index = current_pixel.palette_index,
                        .depth = view_height - current_entity.y +
                                 current_pixel.depth,
                    });
                }
            }
        }
    }
};

// Program.
auto main() -> int {
    std::cout << "Hello, user!\n";

    SpriteAtlas* p_sprite_atlas = new (std::nothrow) SpriteAtlas();

    for (int i = 0; i < p_sprite_atlas->atlas_width; i++) {
        for (int j = 0; j < p_sprite_atlas->atlas_height; j++) {
            p_sprite_atlas->pixels[j][i] = Pixel{
                .palette_index = 0,  // Blank color.
                .depth = 255,        // Maximum depth.
            };
        }
    }
    p_sprite_atlas->make_cube(0, 0);

    GpuPixelBuffer* p_pixel_buffer_data = new (std::nothrow) GpuPixelBuffer();
    p_pixel_buffer_data->clear();

    // Initialize cubes.
    Entity cubes[8];
    for (int i = 0; i < 8; i++) {
        uint32_t rand_x = static_cast<uint32_t>(rand()) % (480u - 20u);
        uint32_t rand_y = static_cast<uint32_t>(rand()) % (300u - 40u);
        cubes[i].x = rand_x;
        cubes[i].y = rand_y;
        cubes[i].z = 100u;
        cubes[i].tex_x = 0u;
        cubes[i].tex_y = 0u;
        cubes[i].tex_w = 20u;
        cubes[i].tex_h = 40u;
    }

    lava::frame_config config;
    config.param.extensions.insert(config.param.extensions.end(),
                                   {
                                       "VK_KHR_get_physical_device_properties2",
                                   });
    lava::app app(config);
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
    uint32_t const workgroup_width = view_width / 4;
    uint32_t const workgroup_height = view_height / 4;
    uint32_t const workgroup_depth = 4;

    lava::graphics_pipeline::ptr raster_pipeline;
    lava::pipeline_layout::ptr raster_pipeline_layout;

    lava::compute_pipeline::ptr compute_pipeline;
    lava::pipeline_layout::ptr compute_pipeline_layout;
    lava::pipeline::shader_stage::ptr shader_stage;

    VkCommandPool p_cmd_pool;

    lava::descriptor::ptr shared_descriptor_layout;
    lava::descriptor::pool::ptr descriptor_pool;
    VkDescriptorSet p_shared_descriptor_set_image = nullptr;
    VkDescriptorSet p_shared_descriptor_set_pixels = nullptr;

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
            lava::file_data(get_run_path() + SHADERS_PATH + "compute.spv"),
            VK_SHADER_STAGE_COMPUTE_BIT);
        compute_pipeline->create();

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
        compute_pipeline->destroy();
        compute_pipeline_layout->destroy();
        raster_pipeline->destroy();
        raster_pipeline_layout->destroy();
    };

    app.on_process = [&](VkCommandBuffer p_cmd_buf, lava::index) {
        // A command buffer is automatically recording.
        compute_pipeline->bind(p_cmd_buf);
        compute_pipeline_layout->bind_descriptor_set(
            p_cmd_buf, p_shared_descriptor_set_image, 0, {},
            VK_PIPELINE_BIND_POINT_COMPUTE);
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

    app.on_update = [&](lava::delta) {
        cubes[1].x += 1;
        p_pixel_buffer_data->clear();
        p_pixel_buffer_data->render_entities(cubes, 8, p_sprite_atlas);
        void* p_data;
        vkMapMemory(app.device->get(),
                    pixel_buffer_staging->get_device_memory(), 0,
                    pixel_buffer_staging->get_size(), 0, &p_data);
        memcpy(p_data, p_pixel_buffer_data, sizeof(*p_pixel_buffer_data));
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
