#include "liblava/base/base.hpp"
#include <cstdlib>
#include <iostream>
#include <liblava/lava.hpp>

struct AssembledVertex {
  float position[3];
};

auto main() -> int {
  std::cout << "Hello, user!\n";

  lava::app app("Render");
  app.setup();

  lava::graphics_pipeline::ptr pipeline;
  lava::pipeline_layout::ptr layout;

  app.on_create = [&]() {
    pipeline = make_graphics_pipeline(app.device);
    pipeline->add_shader(lava::file_data("res/vertex.spv"),
                         VK_SHADER_STAGE_VERTEX_BIT);

    pipeline->add_shader(lava::file_data("res/fragment.spv"),
                         VK_SHADER_STAGE_FRAGMENT_BIT);

    pipeline->add_color_blend_attachment();

    pipeline->set_rasterization_cull_mode(VK_CULL_MODE_FRONT_BIT);
    pipeline->set_rasterization_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);

    layout = lava::make_pipeline_layout();

    if (!layout->create(app.device))
      return false;

    pipeline->set_layout(layout);
    pipeline->set_auto_size(true);

    lava::render_pass::ptr render_pass = app.shading.get_pass();

    if (!pipeline->create(render_pass->get()))
      return false;

    render_pass->add_front(pipeline);

    pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
      // Hard-code draw of three verts.
      app.device->call().vkCmdDraw(cmd_buf, 3, 1, 0, 0);
    };

    return true;
  };

  app.on_destroy = [&]() {
    pipeline->destroy();
    layout->destroy();
  };

  app.on_update = [&](lava::delta dt) {
    if (!pipeline->activated())
      return true;

    return true;
  };

  return app.run();
}
