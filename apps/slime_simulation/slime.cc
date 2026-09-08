#include "apps/slime_simulation/slime.hh"
#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/implementation_helpers/implementation_helpers.hh"
#include <cmath>
#include <random>

auto SlimeApp::is_running() -> bool { return true; }

auto SlimeApp::create(SlimeCreateInfo info, VulkanRoot &root)
    -> std::expected<SlimeApp, std::string> {

  auto vertices = std::vector<ImplementationHelp::FragApp::Vertex>{
      ImplementationHelp::FragApp::get_frag_app_vertices()};
  auto indices = std::vector<uint16_t>{
      ImplementationHelp::FragApp::get_frag_app_indices()};

  auto maybe_pipeline_container = GraphicsPipeline::PipelineContainer::create(
      info.pipeline_details, root.device_and_queue.logical(),
      root.swapchain_info.surface_format(), nullptr);

  if (!maybe_pipeline_container)
    return std::unexpected("SlimeApp Pipeline Container Init Error : " +
                           maybe_pipeline_container.error());

  auto extracted_pipeline_container =
      std::move(maybe_pipeline_container.value());

  auto maybe_compute_pipeline_texture =
      ComputePipeline::ComputePipelineContainer::create(
          info.compute_details_texture, root.device_and_queue.logical());

  if (!maybe_compute_pipeline_texture)
    return std::unexpected("SlimeApp Texture Compute Pipeline Init Error : " +
                           maybe_compute_pipeline_texture.error());

  auto extracted_compute_pipeline_texture =
      std::move(maybe_compute_pipeline_texture.value());

  auto maybe_compute_pipeline_slime =
      ComputePipeline::ComputePipelineContainer::create(
          info.compute_details_slime, root.device_and_queue.logical());

  if (!maybe_compute_pipeline_slime)
    return std::unexpected("SlimeApp Slime Compute Pipeline Init Error : " +
                           maybe_compute_pipeline_slime.error());

  auto extracted_compute_pipeline_slime =
      std::move(maybe_compute_pipeline_slime.value());

  auto maybe_command_buffer =
      BufferUtils::CommandPoolAndBuffersContainer::create(
          BufferUtils::CommandBufferContainerCreateInfo{
              .num_frames_in_flight = info.max_frames_in_flight},
          root.device_and_queue.logical(), root.device_and_queue.queue_index());

  if (!maybe_command_buffer)
    return std::unexpected("SlimeApp Command Buffer Init Error : " +
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
    return std::unexpected("SlimeApp Data Buffers Init Error : " +
                           maybe_data_buffers.error());

  auto extraced_data_buffers = std::move(maybe_data_buffers.value());

  auto maybe_sync_objects = SyncObjects::SyncObjectsContainer::create(
      info.max_frames_in_flight, root.swapchain_info.images().size(),
      info.max_frames_in_flight, root.device_and_queue.logical());

  if (!maybe_sync_objects)
    return std::unexpected("SlimeApp Sync Objects Init Error : " +
                           maybe_sync_objects.error());

  auto extracted_sync_objects = std::move(maybe_sync_objects.value());

  // Generate default data for texture mesh and slimes
  auto initial_mesh_state = std::vector<TextureColour>{};
  auto initial_slimes_state = std::vector<Slime>{};

  for (auto i = 0; i < info.sim_height * info.sim_width; i++) {
    initial_mesh_state.emplace_back(TextureColour{.r = 0, .g = 0, .b = 0});
  }

  auto rand_gen = std::mt19937{std::random_device{}()};

  auto angle_distribution = std::uniform_real_distribution<float>(
      0.0f, 2.0f * std::numbers::pi_v<float>);

  auto hoz_distribution =
      std::uniform_real_distribution<float>(0.0f, info.sim_width);
  auto vert_distribution =
      std::uniform_real_distribution<float>(0.0f, info.sim_height);

  const glm::vec2 center{
      static_cast<float>(info.sim_width) / 2.0f,
      static_cast<float>(info.sim_height) / 2.0f,
  };

  const float radius = static_cast<float>(info.sim_width) * 0.2f;

  for (auto i = 0; i < info.num_slimes; i++) {
    auto slime = Slime{};

    slime.family_index = 0;

    slime.position = // center;
        glm::vec2{hoz_distribution(rand_gen), vert_distribution(rand_gen)};

    const glm::vec2 to_center = center - slime.position;

    slime.angle =
        angle_distribution(rand_gen); // std::atan2(to_center.y, to_center.x);

    initial_slimes_state.push_back(slime);
  }

  auto maybe_arbitrary_buffer_data_texture_a =
      BufferUtils::ArbitraryGpuDataContainer<TextureColour>::create(
          BufferUtils::NeededObjects{
              .physical_ref = root.device_and_queue.physical(),
              .logical_ref = root.device_and_queue.logical(),
              .queue_ref = root.device_and_queue.queue(),
              .command_pool = extracted_command_buffers.command_pool()},
          initial_mesh_state);

  auto maybe_arbitrary_buffer_data_texture_b =
      BufferUtils::ArbitraryGpuDataContainer<TextureColour>::create(
          BufferUtils::NeededObjects{
              .physical_ref = root.device_and_queue.physical(),
              .logical_ref = root.device_and_queue.logical(),
              .queue_ref = root.device_and_queue.queue(),
              .command_pool = extracted_command_buffers.command_pool()},
          initial_mesh_state);

  auto maybe_arbitrary_buffer_data_slime =
      BufferUtils::ArbitraryGpuDataContainer<Slime>::create(
          BufferUtils::NeededObjects{
              .physical_ref = root.device_and_queue.physical(),
              .logical_ref = root.device_and_queue.logical(),
              .queue_ref = root.device_and_queue.queue(),
              .command_pool = extracted_command_buffers.command_pool()},
          initial_slimes_state);

  if (!maybe_arbitrary_buffer_data_texture_a)
    return std::unexpected("SlimeApp ArbitraryGpuDataContainer Init Error:" +
                           maybe_arbitrary_buffer_data_texture_a.error());
  if (!maybe_arbitrary_buffer_data_texture_b)
    return std::unexpected("SlimeApp ArbitraryGpuDataContainer Init Error:" +
                           maybe_arbitrary_buffer_data_texture_a.error());
  if (!maybe_arbitrary_buffer_data_slime)
    return std::unexpected("SlimeApp ArbitraryGpuDataContainer Init Error:" +
                           maybe_arbitrary_buffer_data_slime.error());

  auto extracted_arbitrary_buffer_texture_a =
      std::move(maybe_arbitrary_buffer_data_texture_a.value());
  auto extracted_arbitrary_buffer_texture_b =
      std::move(maybe_arbitrary_buffer_data_texture_b.value());
  auto extracted_arbitrary_buffer_slime =
      std::move(maybe_arbitrary_buffer_data_slime.value());

  auto object = SlimeApp(
      std::move(extracted_pipeline_container),
      std::move(extracted_compute_pipeline_texture),
      std::move(extracted_compute_pipeline_slime),
      std::move(extracted_command_buffers), std::move(extraced_data_buffers),
      std::move(extracted_sync_objects),
      std::move(extracted_arbitrary_buffer_texture_a),
      std::move(extracted_arbitrary_buffer_texture_b),
      std::move(extracted_arbitrary_buffer_slime), info.max_frames_in_flight,
      info.default_colour, {info.sim_width, info.sim_height}, info.num_slimes);

  return object;
}

auto SlimeApp::get_current_state(std::shared_ptr<GLFWwindow> window,
                                 const VulkanAppRootRefs root_refs)
    -> std::expected<std::optional<VulkanAppTickState>, std::string> {
  auto &swapchain_state = root_refs.swapchain_state_ref;
  auto &logical_device = root_refs.device_and_queue_ref.logical();
  // Ensures that the viewport updates as the screen changes size
  graphics_pipeline_data.update_dynamic_objects(swapchain_state.dimensions());

  if (glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS) {
    tick = tick + 0.0001;
  }

  a_is_current = (a_is_current == 1) ? 0 : 1;

  // std::println("Tick");
  auto push_constants = SlimePushConstant{
      .sim_width = sim_width,
      .sim_height = sim_height,
      .win_width =
          static_cast<glm::uint32_t>(swapchain_state.dimensions().width),
      .win_height =
          static_cast<glm::uint32_t>(swapchain_state.dimensions().height),
      .slimes = static_cast<glm::uint64_t>(slimes.address(logical_device)),
      .num_slimes = static_cast<glm::uint32_t>(num_slimes),
      .texture_mesh_a =
          static_cast<glm::uint64_t>(texture_mesh_a.address(logical_device)),
      .texture_mesh_b =
          static_cast<glm::uint64_t>(texture_mesh_b.address(logical_device)),
      .alternator = a_is_current,
      .tick = tick};

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

auto SlimeApp::record_command_buffer(SlimePushConstant push_constants,
                                     vk::Image &transition_image,
                                     vk::raii::ImageView &image_view,
                                     vk::Rect2D render_area,
                                     vk::Viewport viewport, vk::Rect2D scissor)
    -> void {
  auto &command_buffer =
      command_pool_and_buffers.get_buffer_ref(current_frame_index);

  command_buffer.reset();
  command_buffer.begin({});

  command_buffer.setViewport(0, viewport);
  command_buffer.setScissor(0, scissor);

  // Forces an integer ceil (assuming the numthredas in shader is 64,1,1)
  auto mesh_num_workgroups =
      (push_constants.sim_width * push_constants.sim_height + 63) / 64;
  auto slimes_num_workgroups = (push_constants.num_slimes + 63) / 64;

  ImplementationHelp::CommandBuffer::record_compute_stage<SlimePushConstant>(
      command_buffer, compute_pipeline_data_mesh.pipeline(),
      compute_pipeline_data_mesh.layout(),
      ImplementationHelp::CommandBuffer::ComputeStage<SlimePushConstant>{
          .workgroups = {static_cast<int>(mesh_num_workgroups), 1, 1},
          .maybe_proceeding_barrier =
              vk::MemoryBarrier2{
                  .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                  .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                  .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                  .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead |
                                   vk::AccessFlagBits2::eShaderStorageWrite}},
      push_constants);

  ImplementationHelp::CommandBuffer::record_compute_stage<SlimePushConstant>(
      command_buffer, compute_pipeline_data_slime.pipeline(),
      compute_pipeline_data_slime.layout(),
      ImplementationHelp::CommandBuffer::ComputeStage<SlimePushConstant>{
          .workgroups = {static_cast<int>(slimes_num_workgroups), 1, 1},
          .maybe_proceeding_barrier =
              vk::MemoryBarrier2{
                  .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                  .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead |
                                   vk::AccessFlagBits2::eShaderStorageWrite,
                  .dstStageMask = vk::PipelineStageFlagBits2::eAllGraphics,
                  .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead}},
      push_constants);

  ImplementationHelp::CommandBuffer::record_graphics_stage<SlimePushConstant>(
      command_buffer, graphics_pipeline_data.pipeline(),
      graphics_pipeline_data.layout(),
      ImplementationHelp::CommandBuffer::GraphicsStage<SlimePushConstant>{
          .transition_image = transition_image,
          .image_view = image_view,
          .render_area = render_area,
          .vertices_buffer_ref = data_buffers.vertices().buffer,
          .indices_buffer_ref = data_buffers.indices().buffer,
          .num_indices = static_cast<uint32_t>(data_buffers.indices().size),
          .indices_type =
              ImplementationHelp::CommandBuffer::IndicesType::INT_16,
          .default_colour = default_colour},
      push_constants);

  command_buffer.end();
}

auto create_slime_app(VulkanRoot &root)
    -> std::expected<SlimeApp, std::string> {

  constexpr uint16_t width = 2560;
  constexpr uint16_t height = 1440;
  constexpr auto shader_path = "shaders/slime.spv";

  auto app_colour_blend_data = vk::PipelineColorBlendAttachmentState{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  auto graphics_pipeline = GraphicsPipeline::PipelineContainerCreateInfo{
      ImplementationHelp::Graphics::get_partial_graphics_pipeline_details<
          ImplementationHelp::FragApp::Vertex>(shader_path, shader_path,
                                               "vertMain", "fragMain", 1920,
                                               1080, app_colour_blend_data)};

  graphics_pipeline.push_constant_ranges = std::vector{
      vk::PushConstantRange{.stageFlags = vk::ShaderStageFlagBits::eFragment,
                            .offset = 0,
                            .size = sizeof(SlimePushConstant)}};

  auto app_create_info = SlimeCreateInfo{
      .pipeline_details = graphics_pipeline,
      .default_colour = vk::ClearColorValue(0.2, 0.2, 0.2, 1.0f),
      .max_frames_in_flight = 2,
      .compute_details_texture =
          ComputePipeline::ComputePipelineCreateInfo{
              .compute_shader_path = shader_path,
              .compute_main_func = "compTextureMain",
              .push_constant_ranges = {vk::PushConstantRange{
                  .stageFlags = vk::ShaderStageFlagBits::eCompute,
                  .offset = 0,
                  .size = sizeof(SlimePushConstant)}}},
      .compute_details_slime =
          ComputePipeline::ComputePipelineCreateInfo{
              .compute_shader_path = shader_path,
              .compute_main_func = "compSlimeMain",
              .push_constant_ranges = {vk::PushConstantRange{
                  .stageFlags = vk::ShaderStageFlagBits::eCompute,
                  .offset = 0,
                  .size = sizeof(SlimePushConstant)}}},
      .sim_width = width,
      .sim_height = height,
      .num_slimes = 1000000,
  };

  auto maybe_app = SlimeApp::create(app_create_info, root);

  return maybe_app;
}
