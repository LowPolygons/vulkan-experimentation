#ifndef VULKAN_WRAPPER_SHADER_HASHING_APP_HH
#define VULKAN_WRAPPER_SHADER_HASHING_APP_HH

#include "vulkan/vulkan.hpp"
#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/buffers/command_buffer_container.hh"
#include "vulkan_wrapper/buffers/data_buffer_container.hh"
#include "vulkan_wrapper/implementation_helpers/implementation_helpers.hh"
#include "vulkan_wrapper/pipeline/compute_pipeline_container.hh"
#include "vulkan_wrapper/pipeline/graphics_pipeline_container.hh"
#include "vulkan_wrapper/syncs/sync_object_container.hh"
#include "vulkan_wrapper/wrapper_boilerplate.hh"
#include <glm/glm.hpp>

struct ShaderHashPushConstant {
  glm::uint32_t sim_width;
  glm::uint32_t sim_height;
  glm::uint32_t win_width;
  glm::uint32_t win_height;

  vk::DeviceAddress colour_address;

  glm::uint32_t seed_offset;
};

// This is the type used in the buffer address
struct ShaderHashTextureColour {
  glm::uint8_t r;
  glm::uint8_t g;
  glm::uint8_t b;
};

struct ShaderHashCreateInfo {
  vk::ShaderStageFlagBits buffer_stage;
  GraphicsPipeline::PipelineContainerCreateInfo pipeline_details;
  vk::ClearColorValue default_colour;
  std::size_t max_frames_in_flight;
  ComputePipeline::ComputePipelineCreateInfo compute_details;

  uint32_t sim_width;
  uint32_t sim_height;
};

struct ShaderHashApp : public VulkanAppInterface {
  static_assert(sizeof(ShaderHashPushConstant) <= 128);

public:
  // Vertices and Indices are defined inside it
  static auto create(ShaderHashCreateInfo info, VulkanRoot &root)
      -> std::expected<ShaderHashApp, std::string>;

  // Vulkan Interface requirements
  bool is_running() override;
  auto get_current_state(std::shared_ptr<GLFWwindow> window,
                         const VulkanAppRootRefs root_refs)
      -> std::expected<std::optional<VulkanAppTickState>, std::string> override;

  // Would be in the interface, perhaps might make its own separate interface
  // with generics if possible
  auto record_command_buffer(ShaderHashPushConstant push_constants,
                             vk::Image &transition_image,
                             vk::raii::ImageView &image_view,
                             vk::Rect2D render_area, vk::Viewport viewport,
                             vk::Rect2D scissor) -> void;

  ShaderHashApp(
      GraphicsPipeline::PipelineContainer &&g_p_a,
      ComputePipeline::ComputePipelineContainer &&c_p_d,
      BufferUtils::CommandPoolAndBuffersContainer &&c_p_a_b,
      BufferUtils::DataBufferContainer<ImplementationHelp::FragApp::Vertex,
                                       uint16_t> &&d_b,
      SyncObjects::SyncObjectsContainer &&s_o,
      BufferUtils::ArbitraryGpuDataContainer<ShaderHashTextureColour> s_b,
      uint32_t m_f_i_f, vk::ClearColorValue d_c,
      std::pair<uint32_t, uint32_t> sim_size)
      : graphics_pipeline_data(std::move(g_p_a)),
        compute_pipeline_data(std::move(c_p_d)),
        command_pool_and_buffers(std::move(c_p_a_b)),
        data_buffers(std::move(d_b)), sync_objects(std::move(s_o)),
        max_frames_in_flight(m_f_i_f), default_colour(d_c),
        sim_width(sim_size.first), sim_height(sim_size.second), seed_offset(0),
        shader_buffer(std::move(s_b)) {}

private:
  // Vulkan wrappers
  GraphicsPipeline::PipelineContainer graphics_pipeline_data;
  ComputePipeline::ComputePipelineContainer compute_pipeline_data;
  BufferUtils::CommandPoolAndBuffersContainer command_pool_and_buffers;
  BufferUtils::DataBufferContainer<ImplementationHelp::FragApp::Vertex,
                                   uint16_t>
      data_buffers;
  SyncObjects::SyncObjectsContainer sync_objects;

  // Pipeline metadata
  uint32_t max_frames_in_flight;
  uint32_t current_frame_index = 0;
  vk::ClearColorValue default_colour;

  // Sim related data
  uint32_t sim_width;
  uint32_t sim_height;
  uint32_t seed_offset;

  BufferUtils::ArbitraryGpuDataContainer<ShaderHashTextureColour> shader_buffer;
};

auto create_shader_hash_app(VulkanRoot &root)
    -> std::expected<ShaderHashApp, std::string>;

#endif
