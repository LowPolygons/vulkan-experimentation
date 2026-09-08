#include "apps/shader_hashing/shader_hash.hh"
#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/buffers/transition_buffer_layout.hh"
#include "vulkan_wrapper/implementation_helpers/implementation_helpers.hh"
#include <iostream>
#include <print>

auto ShaderHashApp::is_running() -> bool { return true; }

auto ShaderHashApp::create(ShaderHashCreateInfo info, VulkanRoot &root)
    -> std::expected<ShaderHashApp, std::string> {

  auto vertices = ImplementationHelp::FragApp::get_frag_app_vertices();
  auto indices = ImplementationHelp::FragApp::get_frag_app_indices();

  auto maybe_pipeline_container = GraphicsPipeline::PipelineContainer::create(
      info.pipeline_details, root.device_and_queue.logical(),
      root.swapchain_info.surface_format(), nullptr);

  if (!maybe_pipeline_container)
    return std::unexpected("ShaderHashApp Pipeline Container Init Error : " +
                           maybe_pipeline_container.error());

  auto extracted_pipeline_container =
      std::move(maybe_pipeline_container.value());

  auto maybe_compute_pipeline =
      ComputePipeline::ComputePipelineContainer::create(
          info.compute_details, root.device_and_queue.logical());

  if (!maybe_compute_pipeline)
    return std::unexpected("ShaderHashApp Compute Pipeline Init Error : " +
                           maybe_compute_pipeline.error());

  auto extracted_compute_pipeline = std::move(maybe_compute_pipeline.value());

  auto maybe_command_buffer =
      BufferUtils::CommandPoolAndBuffersContainer::create(
          BufferUtils::CommandBufferContainerCreateInfo{
              .num_frames_in_flight = info.max_frames_in_flight},
          root.device_and_queue.logical(), root.device_and_queue.queue_index());

  if (!maybe_command_buffer)
    return std::unexpected("ShaderHashApp Command Buffer Init Error : " +
                           maybe_command_buffer.error());

  auto extracted_command_buffers = std::move(maybe_command_buffer.value());

  auto maybe_data_buffers =
      BufferUtils::DataBufferContainer<ImplementationHelp::FragApp::Vertex,
                                       uint16_t>::
          create(
              BufferUtils::BufferContainerCreateInfo<
                  ImplementationHelp::FragApp::Vertex, unsigned short>{
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
    return std::unexpected("ShaderHashApp Data Buffers Init Error : " +
                           maybe_data_buffers.error());

  auto extraced_data_buffers = std::move(maybe_data_buffers.value());

  auto maybe_sync_objects = SyncObjects::SyncObjectsContainer::create(
      info.max_frames_in_flight, root.swapchain_info.images().size(),
      info.max_frames_in_flight, root.device_and_queue.logical());

  if (!maybe_sync_objects)
    return std::unexpected("ShaderHashApp Sync Objects Init Error : " +
                           maybe_sync_objects.error());

  auto extracted_sync_objects = std::move(maybe_sync_objects.value());

  // Generate default data
  auto initial_state = std::vector<ShaderHashTextureColour>{{}};

  for (auto i = 0; i < info.sim_height * info.sim_width; i++) {
    initial_state.emplace_back(
        ShaderHashTextureColour{.r = 123, .g = 10, .b = 201});
  }

  auto maybe_arbitrary_buffer_data =
      BufferUtils::ArbitraryGpuDataContainer<ShaderHashTextureColour>::create(
          BufferUtils::NeededObjects{
              .physical_ref = root.device_and_queue.physical(),
              .logical_ref = root.device_and_queue.logical(),
              .queue_ref = root.device_and_queue.queue(),
              .command_pool = extracted_command_buffers.command_pool()},
          initial_state);

  if (!maybe_arbitrary_buffer_data)
    return std::unexpected(
        "ShaderHashApp ArbitraryGpuDataContainer Init Error:" +
        maybe_arbitrary_buffer_data.error());

  auto extracted_arbitrary_buffer =
      std::move(maybe_arbitrary_buffer_data.value());

  auto object = ShaderHashApp(
      std::move(extracted_pipeline_container),
      std::move(extracted_compute_pipeline),
      std::move(extracted_command_buffers), std::move(extraced_data_buffers),
      std::move(extracted_sync_objects), std::move(extracted_arbitrary_buffer),
      info.max_frames_in_flight, info.default_colour,
      {info.sim_width, info.sim_height});

  return object;
}

auto ShaderHashApp::get_current_state(std::shared_ptr<GLFWwindow> window,
                                      const VulkanAppRootRefs root_refs)
    -> std::expected<std::optional<VulkanAppTickState>, std::string> {
  auto &swapchain_state = root_refs.swapchain_state_ref;
  auto &logical_device = root_refs.device_and_queue_ref.logical();
  // Ensures that the viewport updates as the screen changes size
  graphics_pipeline_data.update_dynamic_objects(swapchain_state.dimensions());

  if (glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS) {
    seed_offset++;
  }

  auto push_constants = ShaderHashPushConstant{
      .sim_width = sim_width,
      .sim_height = sim_height,
      .win_width =
          static_cast<glm::uint32_t>(swapchain_state.dimensions().width),
      .win_height =
          static_cast<glm::uint32_t>(swapchain_state.dimensions().height),
      .colour_address =
          static_cast<glm::uint64_t>(shader_buffer.address(logical_device)),
      .seed_offset = seed_offset};

  // This stuff is mostly the same across apps
  auto &fence_ref = sync_objects.fence(current_frame_index);

  auto fence_result =
      logical_device.waitForFences(*fence_ref, vk::True, UINT64_MAX);

  if (fence_result != vk::Result::eSuccess)
    return std::unexpected("Failed to wait for the draw fences");

  logical_device.resetFences(*fence_ref);

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

  record_command_buffer(push_constants, swapchain_state.images()[image_index],
                        swapchain_state.image_views()[image_index],
                        graphics_pipeline_data.scissor(),
                        graphics_pipeline_data.viewport(),
                        graphics_pipeline_data.scissor());

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

auto ShaderHashApp::record_command_buffer(ShaderHashPushConstant push_constants,
                                          vk::Image &transition_image,
                                          vk::raii::ImageView &image_view,
                                          vk::Rect2D render_area,
                                          vk::Viewport viewport,
                                          vk::Rect2D scissor) -> void {
  auto &command_buffer =
      command_pool_and_buffers.get_buffer_ref(current_frame_index);

  auto num_workgroups =
      // Forces an integer ceil (assuming the numthredas in shader is 64,1,1)
      (push_constants.sim_width * push_constants.sim_height + 63) / 64;

  command_buffer.reset();
  command_buffer.begin({});

  command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute,
                              compute_pipeline_data.pipeline());
  command_buffer.pushConstants(compute_pipeline_data.layout(),
                               vk::ShaderStageFlagBits::eCompute, 0,
                               sizeof(ShaderHashPushConstant), &push_constants);
  command_buffer.dispatch(num_workgroups, 1, 1);

  // Basically saying that the graphics stage of the pipeline needs to read
  // memory that a compute shader wrote so it needs synchronisin2654435779ug
  auto pipeline_barrier_info = vk::MemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eAllGraphics,
      .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead};

  command_buffer.pipelineBarrier2(vk::DependencyInfo{
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &pipeline_barrier_info,
  });

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
                              graphics_pipeline_data.pipeline());
  command_buffer.pushConstants(graphics_pipeline_data.layout(),
                               vk::ShaderStageFlagBits::eFragment, 0,
                               sizeof(ShaderHashPushConstant), &push_constants);

  command_buffer.setViewport(0, viewport);
  command_buffer.setScissor(0, scissor);

  command_buffer.bindVertexBuffers(0, *data_buffers.vertices().buffer, {0});
  command_buffer.bindIndexBuffer(data_buffers.indices().buffer, 0,
                                 vk::IndexType::eUint16);

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

auto create_shader_hash_app(VulkanRoot &root)
    -> std::expected<ShaderHashApp, std::string> {
  auto app_colour_blend_data = vk::PipelineColorBlendAttachmentState{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  uint16_t width = 1920;
  uint16_t height = 1080;

  auto app_create_info = ShaderHashCreateInfo{
      .pipeline_details =
          GraphicsPipeline::PipelineContainerCreateInfo{
              .vertex_shader_path = "shaders/shader_hash.spv",
              .frag_shader_path = "shaders/shader_hash.spv",
              .vertex_main_func_name = "vertMain",
              .frag_main_func_name = "fragMain",
              .binding_description = ImplementationHelp::FragApp::Vertex::
                  get_binding_descriptions(),
              .attribute_descriptions = ImplementationHelp::FragApp::Vertex::
                  get_attribute_descriptions(),
              .screen_region = {0, 0, 800, 600, 0, 1},
              // WARN: This causes device lost errors
              .image_slice = {vk::Offset2D(0, 0), {800, 600}},
              .polygon_mode = vk::PolygonMode::eFill,
              .cull_mode = vk::CullModeFlagBits::eBack,
              .front_face = vk::FrontFace::eClockwise,
              .colour_blend_data =
                  vk::PipelineColorBlendStateCreateInfo{
                      .logicOpEnable = vk::False,
                      .logicOp = vk::LogicOp::eClear,
                      .attachmentCount = 1,
                      .pAttachments = &app_colour_blend_data},
              .push_constant_ranges = std::vector{vk::PushConstantRange{
                  .stageFlags = vk::ShaderStageFlagBits::eFragment,
                  .offset = 0,
                  .size = sizeof(ShaderHashPushConstant)}},
          },
      .default_colour = vk::ClearColorValue(0.2, 0.2, 0.2, 1.0f),
      .max_frames_in_flight = 2,
      .compute_details =
          ComputePipeline::ComputePipelineCreateInfo{
              .compute_shader_path = "shaders/shader_hash.spv",
              .compute_main_func = "compMain",
              .push_constant_ranges = {vk::PushConstantRange{
                  .stageFlags = vk::ShaderStageFlagBits::eCompute,
                  .offset = 0,
                  .size = sizeof(ShaderHashPushConstant)}}},
      .sim_width = width,
      .sim_height = height};

  auto maybe_app = ShaderHashApp::create(app_create_info, root);

  return maybe_app;
}
