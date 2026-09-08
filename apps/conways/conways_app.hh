#ifndef VULKAN_WRAPPER_COMPUTE_AND_FRAG_TEST_APP_HH
#define VULKAN_WRAPPER_COMPUTE_AND_FRAG_TEST_APP_HH

#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/buffers/command_buffer_container.hh"
#include "vulkan_wrapper/buffers/data_buffer_container.hh"
#include "vulkan_wrapper/implementation_helpers/implementation_helpers.hh"
#include "vulkan_wrapper/pipeline/compute_pipeline_container.hh"
#include "vulkan_wrapper/pipeline/graphics_pipeline_container.hh"
#include "vulkan_wrapper/syncs/sync_object_container.hh"
#include "vulkan_wrapper/wrapper_boilerplate.hh"
#include <glm/fwd.hpp>
#include <glm/glm.hpp>

// Push constants
struct ConwaysState {
  // Size of the sim, stretched/shrank to fit the screen size
  glm::uint32_t sim_width;
  glm::uint32_t sim_height;
  // Conways needs a previous and current state buffer
  vk::DeviceAddress state_address_a;
  vk::DeviceAddress state_address_b;
  // Just a 1 or 0 to use as a tracker for which buffer is current
  glm::uint32_t alternator;
  // Actual window size
  glm::f32 win_x;
  glm::f32 win_y;
  // Currently Unused
  glm::f32 time;
  glm::f32 ___padding___;
  // Mouse X/Y are relative to the window size, the shader handles the scaling
  glm::uint32_t mouse_x;
  glm::uint32_t mouse_y;
  glm::uint32_t mouse_down;
  // Paused by default, holding space plays the sim
  glm::uint32_t paused;
};

struct ConwaysCreateInfo {
  vk::ShaderStageFlagBits buffer_stage;
  GraphicsPipeline::PipelineContainerCreateInfo pipeline_details;
  vk::ClearColorValue default_colour;
  std::size_t max_frames_in_flight;
  ComputePipeline::ComputePipelineCreateInfo compute_details;

  uint16_t sim_width;
  uint16_t sim_height;
  std::vector<uint8_t> initial_state;
};

struct ConwaysGameOfLife : public VulkanAppInterface {
  static_assert(sizeof(ConwaysState) <= 128);

public:
  static auto create(ConwaysCreateInfo info, VulkanRoot &root,
                     std::vector<ImplementationHelp::FragApp::Vertex> vertices,
                     std::vector<uint16_t> indices)
      -> std::expected<ConwaysGameOfLife, std::string>;

  bool is_running() override;
  auto get_current_state(std::shared_ptr<GLFWwindow> window,
                         const VulkanAppRootRefs root_refs)
      -> std::expected<std::optional<VulkanAppTickState>, std::string> override;

  auto record_command_buffer(ConwaysState push_constants,
                             vk::Image &transition_image,
                             vk::raii::ImageView &image_view,
                             vk::Rect2D render_area, vk::Viewport viewport,
                             vk::Rect2D scissor) -> void;

  ConwaysGameOfLife(
      GraphicsPipeline::PipelineContainer &&g_p_a,
      ComputePipeline::ComputePipelineContainer &&c_p_d,
      BufferUtils::CommandPoolAndBuffersContainer &&c_p_a_b,
      BufferUtils::DataBufferContainer<ImplementationHelp::FragApp::Vertex,
                                       uint16_t> &&d_b,
      SyncObjects::SyncObjectsContainer &&s_o, uint32_t m_f_i_f,
      vk::ClearColorValue d_c, std::pair<uint16_t, uint16_t> sim_state,
      BufferUtils::ArbitraryGpuDataContainer<glm::uint8> &&c_s_b_a,
      BufferUtils::ArbitraryGpuDataContainer<glm::uint8> &&c_s_b_b)
      : graphics_pipeline_data(std::move(g_p_a)),
        compute_pipeline_data(std::move(c_p_d)),
        command_pool_and_buffers(std::move(c_p_a_b)),
        data_buffers(std::move(d_b)), sync_objects(std::move(s_o)),
        max_frames_in_flight(m_f_i_f), current_frame_index(0),
        default_colour(d_c), sim_width(sim_state.first),
        sim_height(sim_state.second),
        compute_shader_buffer_a(std::move(c_s_b_a)),
        compute_shader_buffer_b(std::move(c_s_b_b)),
        a_is_current_not_prev(true) {}

private:
  GraphicsPipeline::PipelineContainer graphics_pipeline_data;
  ComputePipeline::ComputePipelineContainer compute_pipeline_data;
  BufferUtils::CommandPoolAndBuffersContainer command_pool_and_buffers;
  BufferUtils::DataBufferContainer<ImplementationHelp::FragApp::Vertex,
                                   uint16_t>
      data_buffers;
  SyncObjects::SyncObjectsContainer sync_objects;

  uint32_t max_frames_in_flight;
  uint32_t current_frame_index = 0;

  vk::ClearColorValue default_colour;

  // Compute Shader Things
  uint16_t sim_width;
  uint16_t sim_height;
  BufferUtils::ArbitraryGpuDataContainer<glm::uint8_t> compute_shader_buffer_a;
  BufferUtils::ArbitraryGpuDataContainer<glm::uint8_t> compute_shader_buffer_b;
  bool a_is_current_not_prev;
};

auto create_conways_app(VulkanRoot &root)
    -> std::expected<ConwaysGameOfLife, std::string>;
#endif
