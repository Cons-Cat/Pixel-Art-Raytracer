#include <cmath>
#include <cstdlib>
#include <iostream>
#include <liblava/lava.hpp>

// Linux-specific executable path.
#include <unistd.h>
auto get_exe_path() -> std::string {
  std::array<char, PATH_MAX - NAME_MAX> result{};
  ssize_t count =
      readlink("/proc/self/exe", result.data(), PATH_MAX - NAME_MAX);
  std::string full_path = std::string(result.data());
  return std::string(full_path.substr(0, full_path.find_last_of('/'))) + "/";
}

auto main() -> int {
  std::cout << "Hello, user!\n";

  constexpr uint32_t width = 480;
  constexpr uint32_t height = 300;

  lava::app app("Render");
  app.setup();
  app.window.set_size(width, height);

  uint32_t max_workgroups =
      app.device->get_properties().limits.maxComputeWorkGroupInvocations;
  std::cout << "Maximum workgroups: " << max_workgroups << "\n";
  uint32_t sqrt_max_workgroups = std::sqrt(max_workgroups);
  std::cout << "Sqrt maximum workgroups: " << sqrt_max_workgroups << "\n";
  uint32_t workgroup_size = width * height / 64 / max_workgroups * 8;
  std::cout << "Workgroup size: " << workgroup_size << "\n";

  // workgroup_size = sqrt_max_workgroups;

  VkCommandPool cmd_pool;

  lava::graphics_pipeline::ptr raster_pipeline;
  lava::pipeline_layout::ptr raster_pipeline_layout;

  lava::compute_pipeline::ptr compute_pipeline;
  lava::pipeline_layout::ptr compute_pipeline_layout;
  lava::pipeline::shader_stage::ptr shader_stage;

  lava::descriptor::ptr image_descriptor_layout;
  lava::descriptor::ptr image_descriptor_layout_compute;
  lava::descriptor::pool::ptr descriptor_pool;
  VkDescriptorSet shared_descriptor_set = nullptr;
  VkDescriptorSet shared_descriptor_set_compute = nullptr;

  // uint8_t image_data[width * height]{};
  lava::image storage_image(VK_FORMAT_R8G8B8A8_UNORM);
  storage_image.set_usage(VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT);

  app.on_create = [&]() {
    descriptor_pool = lava::make_descriptor_pool();
    descriptor_pool->create(app.device,
                            {
                                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
                            },
                            2);

    image_descriptor_layout = lava::make_descriptor();
    image_descriptor_layout->add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                         VK_SHADER_STAGE_FRAGMENT_BIT);
    image_descriptor_layout->create(app.device);

    image_descriptor_layout_compute = lava::make_descriptor();
    image_descriptor_layout_compute->add_binding(
        0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    image_descriptor_layout_compute->create(app.device);

    storage_image.create(app.device, {width, height});

    lava::block block;
    {
      block.create(app.device, 1, app.device->graphics_queue().family);
      auto id = block.add_command([&](VkCommandBuffer cmd_buf) {
        lava::set_image_layout(
            app.device, cmd_buf, storage_image.get(), VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        std::cout << "Howdy\n";
      });
      block.process(0);

      // TODO: Replace this with lava's one shot command buffer.
      {
        auto cmd_buf_temp = block.get_command_buffer(id);
        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd_buf_temp,
        };
        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fence_info;
        fence_info.pNext = nullptr;
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = 0; // VK_FLAGS_NONE;
        VkFence fence;
        vkCreateFence(app.device->get(), &fence_info, nullptr, &fence);
        // Submit to the queue
        vkQueueSubmit(app.device->graphics_queue().vk_queue, 1, &submit_info,
                      fence);
        // Wait for the fence to signal that command buffer has finished
        // executing
        vkWaitForFences(app.device->get(), 1, &fence, VK_TRUE, 100000000000);
        vkDestroyFence(app.device->get(), fence, nullptr);
        block.destroy();
      }
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = storage_image.get(),
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = storage_image.get_format(),
        .components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                       VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    VkImageView view;
    vkCreateImageView(app.device->get(), &view_info, nullptr, &view);

    VkDescriptorImageInfo descriptor_image_info = {
        // .imageView = storage_image.get_view(),
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    // Create the descriptor set.
    shared_descriptor_set =
        image_descriptor_layout->allocate(descriptor_pool->get());
    VkWriteDescriptorSet const write_desc_storage_image{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = shared_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &descriptor_image_info,
    };

    // TODO: Should be able to reuse shared_descriptor_set instead.
    // Create the descriptor set.
    shared_descriptor_set_compute =
        image_descriptor_layout_compute->allocate(descriptor_pool->get());
    VkWriteDescriptorSet const write_desc_storage_image_compute{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = shared_descriptor_set_compute,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &descriptor_image_info,
    };

    app.device->vkUpdateDescriptorSets(
        {write_desc_storage_image, write_desc_storage_image_compute});

    // Create compute pipeline.
    compute_pipeline = lava::make_compute_pipeline(app.device);
    compute_pipeline_layout = lava::make_pipeline_layout();
    compute_pipeline_layout->add_descriptor(image_descriptor_layout_compute);
    compute_pipeline_layout->create(app.device);
    compute_pipeline->set_layout(compute_pipeline_layout);
    compute_pipeline->set_shader_stage(
        lava::file_data(get_exe_path() + "../../res/compute.spv"),
        VK_SHADER_STAGE_COMPUTE_BIT);
    compute_pipeline->create();

    // Make raster pipeline.
    raster_pipeline = lava::make_graphics_pipeline(app.device);
    raster_pipeline->add_shader(
        lava::file_data(get_exe_path() + "../../res/vertex.spv"),
        VK_SHADER_STAGE_VERTEX_BIT);
    raster_pipeline->add_shader(
        lava::file_data(get_exe_path() + "../../res/fragment.spv"),
        VK_SHADER_STAGE_FRAGMENT_BIT);
    raster_pipeline->add_color_blend_attachment();
    raster_pipeline->set_rasterization_cull_mode(VK_CULL_MODE_FRONT_BIT);
    raster_pipeline->set_rasterization_front_face(
        VK_FRONT_FACE_COUNTER_CLOCKWISE);
    raster_pipeline_layout = lava::make_pipeline_layout();
    raster_pipeline_layout->add_descriptor(image_descriptor_layout);
    raster_pipeline_layout->create(app.device);
    raster_pipeline->set_layout(raster_pipeline_layout);
    raster_pipeline->set_auto_size(true);
    lava::render_pass::ptr render_pass = app.shading.get_pass();
    raster_pipeline->create(render_pass->get());
    render_pass->add_front(raster_pipeline);

    // Hard-code draw of three verts.
    raster_pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
      raster_pipeline_layout->bind(cmd_buf, shared_descriptor_set);
      app.device->call().vkCmdDraw(cmd_buf, 3, 1, 0, 0);
    };

    std::cout << "We made it!\n";
    return true;
  };

  app.on_destroy = [&]() {
    compute_pipeline->destroy();
    compute_pipeline_layout->destroy();
    raster_pipeline->destroy();
    raster_pipeline_layout->destroy();
  };

  app.on_process = [&](VkCommandBuffer cmd_buf, lava::index) {
    compute_pipeline->bind(cmd_buf);
    compute_pipeline_layout->bind_descriptor_set(
        cmd_buf, shared_descriptor_set_compute, 0, {},
        VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd_buf, width / workgroup_size, height / workgroup_size, 1);

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
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &image_memory_barrier);
  };

  app.on_update = [&](lava::delta dt) {
    if (!compute_pipeline->activated()) {
      return false;
    }
    if (!raster_pipeline->activated()) {
      return false;
    }
    return true;
  };

  return app.run();
}
