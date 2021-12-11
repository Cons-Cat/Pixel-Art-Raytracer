#include "shared_behavior.hpp"
#include "vulkan/vulkan_core.h"

struct Vertex {
    alignas(8) std::array<int32_t, 2> position;
    // This is a pixel offset into the sprite atlas, which is normalized in the
    // shader.
    alignas(8) std::array<float, 2> uv;
};

struct Quad {
    // TODO: Tilt this 45d along x-axis.
    std::vector<Vertex> vertices = {{{0, 0}, {0, 0}},
                                    {{20, 0}, {20, 0}},
                                    {{0, 20}, {0, 20}},
                                    {{20, 20}, {20, 20}}};
    // Clockwise winding order.
    std::vector<int32_t> indices = {0, 1, 3, 3, 2, 0};
};

struct Pixel {
    // <X, Y, Z>
    alignas(16) std::array<float, 3> color;
};

struct SpriteAtlas {
    static constexpr int32_t sprite_width = 20;
    static constexpr int32_t sprite_height = 20;
    static constexpr int32_t sheet_width = 2;
    static constexpr int32_t sheet_height = 2;
    Pixel pixels[sprite_width * sheet_width * 2][sprite_height * sheet_height];

    void make_cube_top(int32_t x, int32_t y) {
        for (int i = 0; i < sprite_width; i++) {
            for (int j = 0; j < sprite_height; j++) {
                // Normal faces +Y.
                pixels[x + i][y + j] = {0.f, 1.f, 0.f};
                // This is Oklab color space.
                pixels[x + i + sprite_width][y + j] = {0.8222894889915049,
                                                       -0.027784464472867976,
                                                       0.06851569391191548};
            }
        }
    }

    void make_cube_front(int32_t x, int32_t y) {
        for (int i = 0; i < sprite_width; i++) {
            for (int j = 0; j < sprite_height; j++) {
                // Normal faces +Z.
                pixels[x + i][y + j] = {0.f, 0.f, 1.f};
                // This is Oklab color space.
                pixels[x + i + sprite_width][y + j] = {0.8222894889915049,
                                                       -0.027784464472867976,
                                                       0.06851569391191548};
            }
        }
    }
};

struct Entity {
    using Sprite = struct {
        std::array<int32_t, 3> sprite_offset;
        std::array<int32_t, 2> texture_offset;
    };
    int32_t sprites_count;
    Sprite* sprites;  // This is hard-coded for cubes ..
    std::array<int32_t, 3> position;

    void push(std::vector<Vertex>& vertex_buffer,
              std::vector<int32_t>& index_buffer) {
        Quad mesh;
        vertex_buffer.insert(vertex_buffer.end(), mesh.vertices.begin(),
                             mesh.vertices.end());
        index_buffer.insert(index_buffer.end(), mesh.indices.begin(),
                            mesh.indices.end());
    }
};

auto initialize_universe() -> Entity* {
    // Initialize cubes.
    Entity* p_cubes = new (std::nothrow) Entity[8];
    for (int32_t i = 0; i < 8; i++) {
        int32_t rand_x = rand() % (480 - 20);
        int32_t rand_y = (rand() % (300 - 40)) / 2;
        int32_t rand_z = (rand() % (300 - 40)) / 2;
        p_cubes[i].position = {rand_x, rand_y, rand_z};
        p_cubes[i].sprites_count = 2;
        p_cubes[i].sprites =
            new (std::nothrow) Entity::Sprite[p_cubes[i].sprites_count]{
                {// Top face of a cube.
                 .sprite_offset = {0, -20, 0}},
                {// Front face of a cube.
                 {0, 0, 0}},
            };
    }
    return p_cubes;
}

auto main(int argc, char* argv[]) -> int {
    std::cout << "Hello, user!\n";

    lava::app app("reference", {argc, argv});
    app.setup();
    app.window.set_size(view_width, view_height);

    lava::graphics_pipeline::ptr raster_pipeline;
    lava::pipeline_layout::ptr raster_pipeline_layout;

    lava::pipeline::shader_stage::ptr shader_stage;

    VkCommandPool p_cmd_pool;

    lava::descriptor::pool::ptr descriptor_pool;
    lava::descriptor::ptr descriptor_layout;
    VkDescriptorSet p_descriptor_set = nullptr;

    lava::image image(VK_FORMAT_R8G8B8A8_UNORM);
    VkSampler p_texture_sampler;

    // TODO: Stage this buffer.
    lava::buffer::ptr vertex_buffer;
    lava::buffer::ptr index_buffer;

    Entity* p_entities = initialize_universe();
    SpriteAtlas* p_sprite_atlas;
    p_sprite_atlas->make_cube_top(0, 0);
    p_sprite_atlas->make_cube_front(0, 20);

    app.on_create = [&]() {
        descriptor_pool = lava::make_descriptor_pool();
        descriptor_pool->create(app.device,
                                {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                 {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}},
                                3);

        app.device->vkCreateCommandPool(app.device->graphics_queue().family,
                                        &p_cmd_pool);

        descriptor_layout = lava::make_descriptor();
        descriptor_layout->add_binding(
            0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT);
        descriptor_layout->create(app.device);

        // Make raster pipeline.
        raster_pipeline = lava::make_graphics_pipeline(app.device);
        raster_pipeline->add_shader(
            lava::file_data(get_run_path() + SHADERS_PATH +
                            "reference_vertex.spv"),
            VK_SHADER_STAGE_VERTEX_BIT);
        raster_pipeline->add_shader(
            lava::file_data(get_run_path() + SHADERS_PATH +
                            "reference_fragment.spv"),
            VK_SHADER_STAGE_FRAGMENT_BIT);
        raster_pipeline->set_depth_test_and_write();
        raster_pipeline->set_depth_compare_op(VK_COMPARE_OP_LESS_OR_EQUAL);
        raster_pipeline->set_vertex_input_binding(
            {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX});
        raster_pipeline->set_vertex_input_attributes({
            {0, 0, VK_FORMAT_R32G32B32_SINT,
             lava::to_ui32(offsetof(Vertex, position))},
            {1, 0, VK_FORMAT_R32G32_SFLOAT,
             lava::to_ui32(offsetof(Vertex, uv))},
        });
        raster_pipeline->add_color_blend_attachment();
        // raster_pipeline->set_rasterization_cull_mode(VK_CULL_MODE_FRONT_BIT);
        // raster_pipeline->set_rasterization_front_face(
        //     VK_FRONT_FACE_COUNTER_CLOCKWISE);
        raster_pipeline_layout = lava::make_pipeline_layout();
        raster_pipeline_layout->add_descriptor(descriptor_layout);
        raster_pipeline_layout->create(app.device);
        raster_pipeline->set_layout(raster_pipeline_layout);
        raster_pipeline->set_auto_size(true);

        std::vector<Vertex> entity_vertices;
        std::vector<int32_t> entity_indices;
        for (int i = 0; i < p_entities->sprites_count; i++) {
            p_entities[i].push(entity_vertices, entity_indices);
        }
        vertex_buffer->create(app.device, entity_vertices.data(),
                              sizeof(Vertex) * entity_vertices.size(),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        vertex_buffer->create(app.device, entity_indices.data(),
                              sizeof(int32_t) * entity_indices.size(),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        image.create(app.device, {view_width, view_height});

        VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image.get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image.get_format(),
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

        VkSamplerCreateInfo sampler_create_info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .anisotropyEnable = VK_FALSE,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        vkCreateSampler(app.device->get(), &sampler_create_info, nullptr,
                        &p_texture_sampler);
        VkDescriptorImageInfo sampler_info = {
            .sampler = p_texture_sampler,
        };

        p_descriptor_set = descriptor_layout->allocate(descriptor_pool->get());

        VkWriteDescriptorSet const write_desc_sampler{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = p_descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sampler_info,
        };

        lava::render_pass::ptr render_pass = app.shading.get_pass();
        raster_pipeline->create(render_pass->get());
        render_pass->add_front(raster_pipeline);

        return true;
    };

    return app.run();
}
