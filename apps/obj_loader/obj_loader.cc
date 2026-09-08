#include "obj_loader.hh"
#include "tiny_obj_loader.h"
#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/buffers/data_buffer_container.hh"
#include "vulkan_wrapper/buffers/transition_buffer_layout.hh"
#include "vulkan_wrapper/image/graphics_depth_image_container.hh"
#include "vulkan_wrapper/implementation_helpers/implementation_helpers.hh"
#include "vulkan_wrapper/pipeline/graphics_pipeline_container.hh"

#include <chrono>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

auto ObjLoaderApp::is_running() -> bool { return true; }

auto ObjLoaderApp::get_data_buffer_container(std::string path,
                                             vk::raii::CommandPool &pool,
                                             vk::raii::PhysicalDevice &physical,
                                             vk::raii::Device &logical,
                                             vk::raii::Queue &queue)
    -> std::expected<BufferUtils::DataBufferContainer<Vertex3D, uint32_t>,
                     std::string> {
  // Positions, Normas and Texcoords
  auto attrib = tinyobj::attrib_t{};
  auto shapes = std::vector<tinyobj::shape_t>{};
  auto __materials = std::vector<tinyobj::material_t>{};
  auto warn = std::string{};
  auto error = std::string{};

  auto all_vertices = std::vector<Vertex3D>{};
  auto all_indices = std::vector<uint32_t>{};

  if (!tinyobj::LoadObj(&attrib, &shapes, &__materials, &warn, &error,
                        path.c_str())) {
    return std::unexpected("Failed to load a .obj file: " + error);
  }

  for (const auto &shape : shapes) {
    for (const auto &index : shape.mesh.indices) {
      auto vertex = Vertex3D{};

      // Position should always exist
      if (index.vertex_index < 0) {
        return std::unexpected("OBJ contains an invalid vertex index");
      }

      vertex.position = {
          attrib.vertices[3 * index.vertex_index + 0],
          attrib.vertices[3 * index.vertex_index + 1],
          attrib.vertices[3 * index.vertex_index + 2],
      };

      // Texture coordinate may not exist
      if (index.texcoord_index >= 0) {
        vertex.tex_coord = {
            attrib.texcoords[2 * index.texcoord_index + 0],
            attrib.texcoords[2 * index.texcoord_index + 1],
        };
      } else {
        vertex.tex_coord = {0.0f, 0.0f};
      }

      // Normal may not exist
      if (index.normal_index >= 0) {
        vertex.colour = {
            attrib.normals[3 * index.normal_index + 0],
            attrib.normals[3 * index.normal_index + 1],
            attrib.normals[3 * index.normal_index + 2],
        };
      } else {
        vertex.colour = {1.0f, 1.0f, 1.0f};
      }

      all_vertices.push_back(vertex);
      all_indices.push_back(static_cast<uint32_t>(all_indices.size()));
    }
  }

  std::println("Object Vertices and Indices Count: {}, {}", all_vertices.size(),
               all_indices.size());

  auto maybe_data_container =
      BufferUtils::DataBufferContainer<Vertex3D, uint32_t>::create(
          BufferUtils::BufferContainerCreateInfo<Vertex3D, unsigned int>{
              .num_vertices = all_vertices.size(),
              .num_indices = all_indices.size(),
              .vertex_data = all_vertices,
              .index_data = all_indices},
          BufferUtils::DeviceBundleRefs{.physical_ref = physical,
                                        .logical_ref = logical,
                                        .queue_ref = queue,
                                        .command_pool = pool});

  if (!maybe_data_container)
    return std::unexpected("Failed to load new object: " +
                           maybe_data_container.error());

  auto final_object = std::move(maybe_data_container.value());

  return std::move(final_object);
}

auto ObjLoaderApp::update_mvp_object() -> ObjLoaderUniformMVP {

  static auto start_time = std::chrono::high_resolution_clock::now();

  auto current_time = std::chrono::high_resolution_clock::now();

  auto time_diff = std::chrono::duration<float, std::chrono::seconds::period>(
                       current_time - start_time)
                       .count();

  auto ubo = ObjLoaderUniformMVP{};

  float scale = 0.1f;

  ubo.model = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
              glm::rotate(glm::mat4(1.0f), time_diff * glm::radians(90.0f),
                          glm::vec3(1.0f, -5.0f, 5.0f));
  ubo.view =
      glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, 0.0f, 1.0f));

  ubo.proj = glm::perspective(
      glm::radians(45.0f), static_cast<float>(1920) / static_cast<float>(1080),
      0.1f, 10.0f);

  return ubo;
}
auto ObjLoaderApp::create(ObjLoaderAppCreateInfo info, VulkanRoot &root)
    -> std::expected<ObjLoaderApp, std::string> {
  auto maybe_pipeline_container = GraphicsPipeline::PipelineContainer::create(
      info.pipeline_details, root.device_and_queue.logical(),
      root.swapchain_info.surface_format(), nullptr);

  if (!maybe_pipeline_container)
    return std::unexpected("ShaderHashApp Pipeline Container Init Error : " +
                           maybe_pipeline_container.error());

  auto extracted_pipeline_container =
      std::move(maybe_pipeline_container.value());

  auto maybe_command_buffer =
      BufferUtils::CommandPoolAndBuffersContainer::create(
          BufferUtils::CommandBufferContainerCreateInfo{
              .num_frames_in_flight = info.max_frames_in_flight},
          root.device_and_queue.logical(), root.device_and_queue.queue_index());

  if (!maybe_command_buffer)
    return std::unexpected("ShaderHashApp Command Buffer Init Error : " +
                           maybe_command_buffer.error());

  auto extracted_command_buffers = std::move(maybe_command_buffer.value());

  auto maybe_data_buffers = ObjLoaderApp::get_data_buffer_container(
      info.initial_path, extracted_command_buffers.command_pool(),
      root.device_and_queue.physical(), root.device_and_queue.logical(),
      root.device_and_queue.queue());

  if (!maybe_data_buffers)
    return std::unexpected("ObjLoaderApp Data Buffer Init Error: " +
                           maybe_data_buffers.error());

  auto extracted_data_buffers = std::move(maybe_data_buffers.value());

  auto maybe_sync_objects = SyncObjects::SyncObjectsContainer::create(
      info.max_frames_in_flight, root.swapchain_info.images().size(),
      info.max_frames_in_flight, root.device_and_queue.logical());

  if (!maybe_sync_objects)
    return std::unexpected("ShaderHashApp Sync Objects Init Error : " +
                           maybe_sync_objects.error());

  auto extracted_sync_objects = std::move(maybe_sync_objects.value());

  auto mvp = ObjLoaderApp::update_mvp_object();
  auto vector_of_ubo = std::vector{mvp};

  auto maybe_ubo_buffer =
      BufferUtils::ArbitraryGpuDataContainer<ObjLoaderUniformMVP>::create(
          BufferUtils::NeededObjects{
              .physical_ref = root.device_and_queue.physical(),
              .logical_ref = root.device_and_queue.logical(),
              .queue_ref = root.device_and_queue.queue(),
              .command_pool = extracted_command_buffers.command_pool()},
          vector_of_ubo);

  if (!maybe_ubo_buffer)
    return std::unexpected("App UBO Buffer Init Error: " +
                           maybe_ubo_buffer.error());

  auto extraced_ubo_obj = std::move(maybe_ubo_buffer.value());

  auto object = ObjLoaderApp(
      std::move(extracted_pipeline_container),
      std::move(extracted_command_buffers), std::move(extracted_data_buffers),
      std::move(extracted_sync_objects), info.max_frames_in_flight,
      info.default_colour, std::move(extraced_ubo_obj));
  return object;
}

auto ObjLoaderApp::get_current_state(std::shared_ptr<GLFWwindow> window,
                                     VulkanAppRootRefs root_refs)
    -> std::expected<std::optional<VulkanAppTickState>, std::string> {
  // Ensures that the viewport updates as the screen changes size
  graphics_pipeline_data.update_dynamic_objects(
      root_refs.swapchain_state_ref.dimensions());

  auto push_constants = ObjLoaderPushConstants{
      .win_width = root_refs.swapchain_state_ref.dimensions().width,
      .win_height = root_refs.swapchain_state_ref.dimensions().height,
      .address = static_cast<uint64_t>(
          mvp_container.address(root_refs.device_and_queue_ref.logical()))};

  auto new_mvp = ObjLoaderApp::update_mvp_object();
  //
  auto maybe_data_upload_success = mvp_container.override_gpu_data(
      BufferUtils::NeededObjects{
          .physical_ref = root_refs.device_and_queue_ref.physical(),
          .logical_ref = root_refs.device_and_queue_ref.logical(),
          .queue_ref = root_refs.device_and_queue_ref.queue(),
          .command_pool = command_pool_and_buffers.command_pool(),
      },
      std::vector{new_mvp});
  if (!maybe_data_upload_success)
    return std::unexpected("ObjectLoaderApp GPU Data Override Error: " +
                           maybe_data_upload_success.error());

  auto &logical_device = root_refs.device_and_queue_ref.logical();
  // This stuff is mostly the same across apps
  auto &fence_ref = sync_objects.fence(current_frame_index);

  auto fence_result =
      logical_device.waitForFences(*fence_ref, vk::True, UINT64_MAX);

  if (fence_result != vk::Result::eSuccess)
    return std::unexpected("Failed to wait for the draw fences");

  logical_device.resetFences(*fence_ref);

  auto &present_complete_sem =
      sync_objects.present_complete_semaphore(current_frame_index);

  auto [result, image_index] =
      root_refs.swapchain_state_ref.swap_chain().acquireNextImage(
          UINT64_MAX, present_complete_sem, nullptr);

  if (result == vk::Result::eErrorOutOfDateKHR ||
      result == vk::Result::eSuboptimalKHR) {
    // Swap chain requires recreation
    return std::nullopt;
  }

  if (result != vk::Result::eSuccess)
    return std::unexpected("Failed to acquire next swap chain image");

  record_command_buffer(
      push_constants, root_refs.depth_data_container,
      root_refs.swapchain_state_ref.images()[image_index],
      root_refs.swapchain_state_ref.image_views()[image_index],
      graphics_pipeline_data.scissor(), graphics_pipeline_data.viewport(),
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

auto ObjLoaderApp::record_command_buffer(
    ObjLoaderPushConstants push_constants,
    GraphicsPipeline::DepthDataContainer &depth_data_container,
    vk::Image &transition_image, vk::raii::ImageView &image_view,
    vk::Rect2D render_area, vk::Viewport viewport, vk::Rect2D scissor) -> void {
  auto &command_buffer =
      command_pool_and_buffers.get_buffer_ref(current_frame_index);

  command_buffer.reset();
  command_buffer.begin({});

  command_buffer.setViewport(0, viewport);
  command_buffer.setScissor(0, scissor);

  auto depth_attachment_info = vk::RenderingAttachmentInfo{
      .imageView = depth_data_container.view(),
      .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eDontCare,
      .clearValue = vk::ClearDepthStencilValue(1.0f, 0.0)};

  BufferUtils::transition_image_layout_on_buffer(
      command_buffer, depth_data_container.image(), vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthAttachmentOptimal,
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
          vk::PipelineStageFlagBits2::eLateFragmentTests,
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
          vk::PipelineStageFlagBits2::eLateFragmentTests,
      vk::ImageAspectFlagBits::eDepth);

  ImplementationHelp::CommandBuffer::record_graphics_stage<
      ObjLoaderPushConstants>(
      command_buffer, graphics_pipeline_data.pipeline(),
      graphics_pipeline_data.layout(),
      ImplementationHelp::CommandBuffer::GraphicsStage<ObjLoaderPushConstants>{
          .transition_image = transition_image,
          .image_view = image_view,
          .render_area = render_area,
          .vertices_buffer_ref = data_buffers.vertices().buffer,
          .indices_buffer_ref = data_buffers.indices().buffer,
          .num_indices = static_cast<uint32_t>(data_buffers.indices().size),
          .indices_type =
              ImplementationHelp::CommandBuffer::IndicesType::INT_32,
          .default_colour = default_colour},
      push_constants, depth_attachment_info);

  command_buffer.end();
}

auto create_obj_loader_app(VulkanRoot &root)
    -> std::expected<ObjLoaderApp, std::string> {
  constexpr auto inital_object_path =
      "apps/obj_loader/objects/FinalBaseMesh.obj";
  constexpr auto shader_path = "shaders/obj_loader.spv";

  auto app_colour_blend_data = vk::PipelineColorBlendAttachmentState{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  auto graphics_pipeline = GraphicsPipeline::PipelineContainerCreateInfo{
      ImplementationHelp::Graphics::get_partial_graphics_pipeline_details<
          Vertex3D>(shader_path, shader_path, "vertMain", "fragMain", 1920,
                    1080, app_colour_blend_data)};
  graphics_pipeline.push_constant_ranges = std::vector{
      vk::PushConstantRange{.stageFlags = vk::ShaderStageFlagBits::eAllGraphics,
                            .offset = 0,
                            .size = sizeof(ObjLoaderPushConstants)}};
  graphics_pipeline.cull_mode = vk::CullModeFlagBits::eBack;
  graphics_pipeline.use_generic_depth_stencil = true;
  graphics_pipeline.front_face = vk::FrontFace::eClockwise;
  graphics_pipeline.depth_stencil_format = vk::Format::eD32Sfloat;

  auto app_create_info = ObjLoaderAppCreateInfo{
      .buffer_stage = vk::ShaderStageFlagBits::eAllGraphics,
      .pipeline_details = graphics_pipeline,
      .default_colour = vk::ClearColorValue(0.2, 0.2, 0.2, 1.0f),
      .max_frames_in_flight = 2,
      .initial_path = inital_object_path};

  auto maybe_app = ObjLoaderApp::create(app_create_info, root);

  return maybe_app;
}
