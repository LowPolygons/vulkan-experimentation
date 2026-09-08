#ifndef VULKAN_WRAPPER_APP_OBJ_LOADER_HH
#define VULKAN_WRAPPER_APP_OBJ_LOADER_HH

#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/buffers/command_buffer_container.hh"
#include "vulkan_wrapper/buffers/data_buffer_container.hh"
#include "vulkan_wrapper/buffers/uniform_buffer_container.hh"
#include "vulkan_wrapper/image/graphics_depth_image_container.hh"
#include "vulkan_wrapper/pipeline/graphics_pipeline_container.hh"
#include "vulkan_wrapper/syncs/sync_object_container.hh"
#include "vulkan_wrapper/wrapper_boilerplate.hh"
#include <glm/glm.hpp>

struct Vertex3D {
  glm::vec3 position;
  glm::vec2 tex_coord;
  glm::vec3 colour;
  static vk::VertexInputBindingDescription get_binding_descriptions() {
    return {.binding = 0,
            .stride = sizeof(Vertex3D),
            .inputRate = vk::VertexInputRate::eVertex};
  }
  static std::vector<vk::VertexInputAttributeDescription>
  get_attribute_descriptions() {
    return {{
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex3D, position)},
        vk::VertexInputAttributeDescription{.location = 1,
                                            .binding = 0,
                                            .format = vk::Format::eR32G32Sfloat,
                                            .offset =
                                                offsetof(Vertex3D, tex_coord)},
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex3D, colour)},
    }};
  }
};

struct ObjLoaderPushConstants {
  glm::uint32_t win_width;
  glm::uint32_t win_height;
  vk::DeviceAddress address;
};

struct ObjLoaderAppCreateInfo {
  vk::ShaderStageFlagBits buffer_stage;
  GraphicsPipeline::PipelineContainerCreateInfo pipeline_details;
  vk::ClearColorValue default_colour;
  std::size_t max_frames_in_flight;
  std::string initial_path;
  // std::vector<std::string> asset_paths;
};

struct ObjLoaderUniformMVP {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

struct ObjLoaderApp : public VulkanAppInterface {
  static_assert(sizeof(ObjLoaderPushConstants) <= 128);

public:
  // Vertices and Indices are defined inside it
  static auto create(ObjLoaderAppCreateInfo info, VulkanRoot &root)
      -> std::expected<ObjLoaderApp, std::string>;

  // Vulkan Interface requirements
  bool is_running() override;
  auto get_current_state(std::shared_ptr<GLFWwindow> window,
                         VulkanAppRootRefs root_refs)
      -> std::expected<std::optional<VulkanAppTickState>, std::string> override;

  static auto update_mvp_object() -> ObjLoaderUniformMVP;

  auto record_command_buffer(
      ObjLoaderPushConstants push_constants,
      GraphicsPipeline::DepthDataContainer &depth_data_container,
      vk::Image &transition_image, vk::raii::ImageView &image_view,
      vk::Rect2D render_area, vk::Viewport viewport, vk::Rect2D scissor)
      -> void;
  ObjLoaderApp(
      GraphicsPipeline::PipelineContainer &&g_p_a,
      BufferUtils::CommandPoolAndBuffersContainer &&c_p_a_b,
      BufferUtils::DataBufferContainer<Vertex3D, uint32_t> &&d_b,
      SyncObjects::SyncObjectsContainer &&s_o, uint32_t m_f_i_f,
      vk::ClearColorValue d_c,
      BufferUtils::ArbitraryGpuDataContainer<ObjLoaderUniformMVP> &&mvp)
      : graphics_pipeline_data(std::move(g_p_a)),
        command_pool_and_buffers(std::move(c_p_a_b)),
        data_buffers(std::move(d_b)), sync_objects(std::move(s_o)),
        max_frames_in_flight(m_f_i_f), default_colour(d_c),
        mvp_container(std::move(mvp)) {}

private:
  static auto
  get_data_buffer_container(std::string path, vk::raii::CommandPool &pool,
                            vk::raii::PhysicalDevice &physical,
                            vk::raii::Device &logical, vk::raii::Queue &queue)
      -> std::expected<BufferUtils::DataBufferContainer<Vertex3D, uint32_t>,
                       std::string>;
  GraphicsPipeline::PipelineContainer graphics_pipeline_data;
  BufferUtils::CommandPoolAndBuffersContainer command_pool_and_buffers;
  BufferUtils::DataBufferContainer<Vertex3D, uint32_t> data_buffers;
  SyncObjects::SyncObjectsContainer sync_objects;

  // Pipeline metadata
  uint32_t max_frames_in_flight;
  uint32_t current_frame_index = 0;
  vk::ClearColorValue default_colour;

  // Arbitrary buffer instead
  BufferUtils::ArbitraryGpuDataContainer<ObjLoaderUniformMVP> mvp_container;
};

auto create_obj_loader_app(VulkanRoot &root)
    -> std::expected<ObjLoaderApp, std::string>;
#endif
