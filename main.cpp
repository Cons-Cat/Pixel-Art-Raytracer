#include <cstdlib>
#include <iostream>
#include <liblava/lava.hpp>

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

  lava::app app("Render");
  app.setup();

  lava::graphics_pipeline::ptr raster_pipeline;
  lava::pipeline_layout::ptr raster_pipeline_layout;

  lava::compute_pipeline::ptr compute_pipeline;
  lava::pipeline_layout::ptr compute_pipeline_layout;
  lava::pipeline::shader_stage::ptr shader_stage;

  lava::descriptor::ptr image_descriptor_layout;
  lava::descriptor::pool::ptr descriptor_pool;
  VkDescriptorSet shared_descriptor_set = nullptr;

  constexpr uint32_t width = 256;
  constexpr uint32_t height = 256;
  // uint8_t image_data[width * height]{};
  lava::image storage_image(VK_FORMAT_R8G8B8A8_UNORM);
  storage_image.set_usage(VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT);

  /*
   * Write compute shader
   * Make graphics shaders wait on compute shader barrier
   * Create staging buffer for initialized texture data.
   * Create storage image and descriptors
   * Make command buffer to copy staging buffer data to storage image
   * Bind storage image to compute and fragment shaders
   * Make compute shader write to storage image
   * Make fragment shader read from storage image
   */

  app.on_create = [&]() {
    descriptor_pool = lava::make_descriptor_pool();
    descriptor_pool->create(app.device,
                            {
                                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                            });

    image_descriptor_layout = lava::make_descriptor();
    image_descriptor_layout->add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                         VK_SHADER_STAGE_COMPUTE_BIT |
                                             VK_SHADER_STAGE_FRAGMENT_BIT);
    image_descriptor_layout->create(app.device);

    compute_pipeline = lava::make_compute_pipeline(app.device);

    compute_pipeline_layout = lava::make_pipeline_layout();
    compute_pipeline_layout->add(image_descriptor_layout);
    compute_pipeline_layout->create(app.device);
    compute_pipeline->set_layout(compute_pipeline_layout);

    compute_pipeline->set_shader_stage(
        lava::file_data(get_exe_path() + "../../res/compute.spv"),
        VK_SHADER_STAGE_COMPUTE_BIT);

    shared_descriptor_set =
        image_descriptor_layout->allocate(descriptor_pool->get());

    storage_image.create(app.device, {width, height});

    lava::block block;
    // storage_image.set_layout(VK_IMAGE_LAYOUT_GENERAL);

    block.create(app.device, 1, app.device->graphics_queue().family);

    auto id = block.add_command([&](VkCommandBuffer cmd_buf) {
      lava::set_image_layout(
          app.device, cmd_buf, storage_image.get(), VK_IMAGE_ASPECT_COLOR_BIT,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
      std::cout << "Howdy\n";
    });

    block.process(0);
    auto cmd_buf = block.get_command_buffer(id);

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf,
    };
    // Create fence to ensure that the command buffer has finished executing
    VkFenceCreateInfo fenceInfo;
    fenceInfo.pNext = nullptr;
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0; // VK_FLAGS_NONE;
    VkFence fence;
    vkCreateFence(app.device->get(), &fenceInfo, nullptr, &fence);
    // Submit to the queue
    vkQueueSubmit(app.device->graphics_queue().vk_queue, 1, &submitInfo, fence);
    // Wait for the fence to signal that command buffer has finished executing
    vkWaitForFences(app.device->get(), 1, &fence, VK_TRUE, 100000000000);
    vkDestroyFence(app.device->get(), fence, nullptr);
    // if (free) {
    //   vkFreeCommandBuffers(app.device.get(), pool, 1, &cmd_buffer);
    // }
    std::cout << "We made it!\n";
    block.destroy();

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

    VkWriteDescriptorSet const write_desc_storage_image{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = shared_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &descriptor_image_info,
    };

    app.device->vkUpdateDescriptorSets({write_desc_storage_image});

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
    raster_pipeline_layout->add(image_descriptor_layout);
    raster_pipeline_layout->create(app.device);

    raster_pipeline->set_layout(raster_pipeline_layout);
    raster_pipeline->set_auto_size(true);

    lava::render_pass::ptr render_pass = app.shading.get_pass();
    raster_pipeline->create(render_pass->get());
    render_pass->add_front(raster_pipeline);

    // Dispatch shaders
    compute_pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
      compute_pipeline_layout->bind(cmd_buf, shared_descriptor_set);
      return true;
    };

    raster_pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
      // Hard-code draw of three verts.
      raster_pipeline_layout->bind(cmd_buf, shared_descriptor_set);
      app.device->call().vkCmdDraw(cmd_buf, 3, 1, 0, 0);
      return true;
    };

    return true;
  };

  app.on_destroy = [&]() {
    compute_pipeline->destroy();
    compute_pipeline_layout->destroy();
    raster_pipeline->destroy();
    raster_pipeline_layout->destroy();
  };

  app.on_update = [&](lava::delta dt) {
    if (!raster_pipeline->activated())
      return true;

    return true;
  };

  return app.run();
}
