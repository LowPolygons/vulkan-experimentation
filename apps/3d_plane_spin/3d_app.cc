#include "3d_app.hh"
#include "vulkan/vulkan.hpp"
#include "vulkan_wrapper/buffers/transition_buffer_layout.hh"
#include "vulkan_wrapper/buffers/uniform_buffer_container.hh"
#include <chrono>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <vulkan/vulkan_raii.hpp>

auto App3D::create(App3DCreateInfo info, VulkanRoot &root,
                   std::vector<ShaderVertex3D> vertices,
                   std::vector<uint16_t> indices)
    -> std::expected<App3D, std::string> {
  auto uniform_buffer_create_info =
      BufferUtils::UniformBufferContainerCreateInfo{
          .stage = info.buffer_stage,
          .max_frames_in_flight = info.max_frames_in_flight};

  auto maybe_uniform_buffer_container =
      BufferUtils::UniformBufferContainer<App3DUniformBuffer>::create(
          uniform_buffer_create_info, root.device_and_queue.logical(),
          root.device_and_queue.physical());

  if (!maybe_uniform_buffer_container)
    return std::unexpected("3D App Unifom Buffer Init Error: " +
                           maybe_uniform_buffer_container.error());

  auto extracted_uniform_buffer_container =
      std::move(maybe_uniform_buffer_container.value());

  auto maybe_pipeline_container = GraphicsPipeline::PipelineContainer::create(
      info.pipeline_details, root.device_and_queue.logical(),
      root.swapchain_info.surface_format(),
      &extracted_uniform_buffer_container.descriptor_set_layout());

  if (!maybe_pipeline_container)
    return std::unexpected("3D App Pipeline Container Init Error: " +
                           maybe_pipeline_container.error());

  auto extracted_pipeline_container =
      std::move(maybe_pipeline_container.value());

  auto maybe_command_buffer =
      BufferUtils::CommandPoolAndBuffersContainer::create(
          BufferUtils::CommandBufferContainerCreateInfo{
              .num_frames_in_flight = info.max_frames_in_flight},
          root.device_and_queue.logical(), root.device_and_queue.queue_index());

  if (!maybe_command_buffer)
    return std::unexpected("3D App Command Buffer Init Error: " +
                           maybe_command_buffer.error());

  auto extracted_command_buffers = std::move(maybe_command_buffer.value());

  auto maybe_data_buffers =
      BufferUtils::DataBufferContainer<ShaderVertex3D, uint16_t>::create(
          BufferUtils::BufferContainerCreateInfo<ShaderVertex3D,
                                                 unsigned short>{
              .num_vertices = vertices.size(),
              .num_indices = indices.size(),
              .vertex_data = vertices,
              .index_data = indices},
          BufferUtils::DeviceBundleRefs{
              .physical_ref = root.device_and_queue.physical(),
              .logical_ref = root.device_and_queue.logical(),
              .queue_ref = root.device_and_queue.queue(),
              .command_pool = extracted_command_buffers.command_pool()});

  if (!maybe_data_buffers)
    return std::unexpected("3D App Data Buffers Init Error: " +
                           maybe_data_buffers.error());

  auto extraced_data_buffers = std::move(maybe_data_buffers.value());

  auto maybe_sync_objects = SyncObjects::SyncObjectsContainer::create(
      info.max_frames_in_flight, root.swapchain_info.images().size(),
      info.max_frames_in_flight, root.device_and_queue.logical());

  if (!maybe_sync_objects)
    return std::unexpected("3D App Sync Objects Init Error: " +
                           maybe_sync_objects.error());

  auto extracted_sync_objects = std::move(maybe_sync_objects.value());

  auto object =
      App3D(std::move(extracted_uniform_buffer_container),
            std::move(extracted_pipeline_container),
            std::move(extracted_command_buffers),
            std::move(extraced_data_buffers), std::move(extracted_sync_objects),
            info.max_frames_in_flight, info.default_colour);

  return std::move(object);
}

// NOTE: for smaller objects, push constants should be used instead
auto App3D::update_uniform_buffer(uint32_t current_image,
                                  vk::Extent2D &dimensions) -> void {
  static auto start_time = std::chrono::high_resolution_clock::now();

  auto current_time = std::chrono::high_resolution_clock::now();

  auto time_diff = std::chrono::duration<float, std::chrono::seconds::period>(
                       current_time - start_time)
                       .count();

  auto ubo = App3DUniformBuffer{};

  ubo.model = glm::rotate(glm::mat4(1.0f), time_diff * glm::radians(90.0f),
                          glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.view =
      glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, 0.0f, 1.0f));

  ubo.proj = glm::perspective(glm::radians(45.0f),
                              static_cast<float>(dimensions.width) /
                                  static_cast<float>(dimensions.height),
                              0.1f, 10.0f);
  ubo.proj[1][1] *= -1;

  memcpy(uniform_buffer_container.uniform_buffer_mapped(current_image), &ubo,
         sizeof(ubo));
}

auto App3D::get_current_state(std::shared_ptr<GLFWwindow> window,
                              const VulkanAppRootRefs root_refs)
    -> std::expected<std::optional<VulkanAppTickState>, std::string> {
  auto &swapchain_state = root_refs.swapchain_state_ref;
  auto &logical_device = root_refs.device_and_queue_ref.logical();
  //=// Update Uniform Buffers
  pipeline_data.update_dynamic_objects(swapchain_state.dimensions());

  update_uniform_buffer(current_frame_index, swapchain_state.dimensions());

  //=// Vulkan Jargon
  auto &fence_ref = sync_objects.fence(current_frame_index);

  auto fence_result =
      logical_device.waitForFences(*fence_ref, vk::True, UINT64_MAX);

  if (fence_result != vk::Result::eSuccess)
    return std::unexpected("Failed to wait for the draw fences");

  auto &present_complete_sem =
      sync_objects.present_complete_semaphore(current_frame_index);

  auto [result, image_index] = swapchain_state.swap_chain().acquireNextImage(
      UINT64_MAX, present_complete_sem, nullptr);

  if (result == vk::Result::eErrorOutOfDateKHR) {
    // Swap chain requires recreation
    return std::nullopt;
  }

  if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    return std::unexpected("Failed to acquire next swap chain image");

  record_command_buffer(swapchain_state.images()[image_index],
                        swapchain_state.image_views()[image_index],
                        pipeline_data.scissor(), pipeline_data.viewport(),
                        pipeline_data.scissor());

  auto wait_destination_stage_mask =
      vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput};

  auto &render_finished_sem =
      sync_objects.render_finished_semaphore(image_index);

  auto submit_info = vk::SubmitInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*present_complete_sem,
      .pWaitDstStageMask = &wait_destination_stage_mask,
      .commandBufferCount = 1,
      .pCommandBuffers =
          &*command_pool_and_buffers.get_buffer_ref(current_frame_index),
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &*render_finished_sem};

  auto state_info =
      VulkanAppTickState{.fence_ref = fence_ref,
                         .present_complete_sem_ref = present_complete_sem,
                         .render_finished_sem_ref = render_finished_sem,
                         .current_frame_index = current_frame_index,
                         .current_image_index = image_index,
                         .queue_submit_info = submit_info};

  current_frame_index = (current_frame_index + 1) % max_frames_in_flight;

  return state_info;
}

auto App3D::is_running() -> bool { return true; }

auto App3D::record_command_buffer(vk::Image &transition_image,
                                  vk::raii::ImageView &image_view,
                                  vk::Rect2D render_area, vk::Viewport viewport,
                                  vk::Rect2D scissor) -> void {
  //
  auto &command_buffer =
      command_pool_and_buffers.get_buffer_ref(current_frame_index);

  command_buffer.reset();
  command_buffer.begin({});

  BufferUtils::transition_image_layout_on_buffer(
      command_buffer, transition_image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal, {},
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput);

  auto attachment_info = vk::RenderingAttachmentInfo{
      .imageView = image_view,
      .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .clearValue = default_colour,
  };

  auto rendering_info =
      vk::RenderingInfo{.renderArea = render_area,
                        .layerCount = 1,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &attachment_info};

  command_buffer.beginRendering(rendering_info);
  command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                              pipeline_data.pipeline());

  command_buffer.setViewport(0, viewport);
  command_buffer.setScissor(0, scissor);

  command_buffer.bindVertexBuffers(0, *data_buffers.vertices().buffer, {0});
  command_buffer.bindIndexBuffer(data_buffers.indices().buffer, 0,
                                 vk::IndexType::eUint16);

  command_buffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline_data.layout(), 0,
      *uniform_buffer_container.descriptor_set(current_frame_index), nullptr);

  command_buffer.drawIndexed(static_cast<uint32_t>(data_buffers.indices().size),
                             1, 0, 0, 0);

  BufferUtils::transition_image_layout_on_buffer(
      command_buffer, transition_image,
      vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
      vk::AccessFlagBits2::eColorAttachmentWrite, {},
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::PipelineStageFlagBits2::eBottomOfPipe);

  command_buffer.end();
}

auto create_3d_app(VulkanRoot &vulkan_root)
    -> std::expected<App3D, std::string> {
  auto app_colour_blend_data = vk::PipelineColorBlendAttachmentState{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  auto app_create_info = App3DCreateInfo{
      .buffer_stage = vk::ShaderStageFlagBits::eAllGraphics,
      .pipeline_details =
          GraphicsPipeline::PipelineContainerCreateInfo{
              .vertex_shader_path = "shaders/3d_shader.spv",
              .frag_shader_path = "shaders/3d_shader.spv",
              .vertex_main_func_name = "vertMain",
              .frag_main_func_name = "fragMain",
              .binding_description = ShaderVertex3D::get_binding_descriptions(),
              .attribute_descriptions =
                  ShaderVertex3D::get_attribute_descriptions(),
              .screen_region = {0, 0, 800, 600, 0, 1},
              .image_slice = {vk::Offset2D(0, 0), {800, 600}},
              .polygon_mode = vk::PolygonMode::eFill,
              .cull_mode = vk::CullModeFlagBits::eBack,
              .front_face = vk::FrontFace::eCounterClockwise,
              .colour_blend_data =
                  vk::PipelineColorBlendStateCreateInfo{
                      .logicOpEnable = vk::False,
                      .logicOp = vk::LogicOp::eClear,
                      .attachmentCount = 1,
                      .pAttachments = &app_colour_blend_data}},
      .default_colour = vk::ClearColorValue(0.3, 0.3, 0.3, 1.0f),
      .max_frames_in_flight = 2};

  auto maybe_app = App3D::create(app_create_info, vulkan_root,
                                 {{{-0.5, -0.5}, {0.0, 0.0, 0.0}, 5.0},
                                  {{0.5, -0.5}, {1.0, 0.0, 0.0}, 5.0},
                                  {{0.5, 0.5}, {0.0, 1.0, 0.0}, 5.0},
                                  {{-0.5, 0.5}, {0.0, 0.0, 1.0}, 5.0}},
                                 {0, 1, 2, 2, 3, 0});

  return maybe_app;
}
