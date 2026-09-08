#ifndef VULKAN_WRAPPER_SLIME_APP_HH
#define VULKAN_WRAPPER_SLIME_APP_HH

#include "vulkan/vulkan.hpp"
#include "vulkan_wrapper/buffers/arbitrary_gpu_data_buffer.hh"
#include "vulkan_wrapper/buffers/command_buffer_container.hh"
#include "vulkan_wrapper/buffers/data_buffer_container.hh"
#include "vulkan_wrapper/implementation_helpers/implementation_helpers.hh"
#include "vulkan_wrapper/pipeline/compute_pipeline_container.hh"
#include "vulkan_wrapper/pipeline/graphics_pipeline_container.hh"
#include "vulkan_wrapper/syncs/sync_object_container.hh"
#include "vulkan_wrapper/wrapper_boilerplate.hh"
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>

// TODO: this should be an asset for apps that are just frag shaders
struct Vertex {
  glm::vec2 position;
  glm::vec3 colour;
  static vk::VertexInputBindingDescription get_binding_descriptions() {
    return {.binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex};
  }
  static std::vector<vk::VertexInputAttributeDescription>
  get_attribute_descriptions() {
    return {{
        vk::VertexInputAttributeDescription{.location = 0,
                                            .binding = 0,
                                            .format = vk::Format::eR32G32Sfloat,
                                            .offset =
                                                offsetof(Vertex, position)},
        vk::VertexInputAttributeDescription{.location = 1,
                                            .binding = 0,
                                            .format =
                                                vk::Format::eR32G32B32Sfloat,
                                            .offset = offsetof(Vertex, colour)},
    }};
  }
};

struct SlimePushConstant {
  // Window Data
  glm::uint32_t sim_width;
  glm::uint32_t sim_height;
  // Only used by the frag shader
  glm::uint32_t win_width;
  glm::uint32_t win_height;

  // Need one Buffer which contains the Slime positions
  vk::DeviceAddress slimes;
  glm::uint32 num_slimes;

  // Need two buffers which contain a previous and current colour mesh state,
  // and an alternator to indicate which is the current and which is the
  // previous
  vk::DeviceAddress texture_mesh_a;
  vk::DeviceAddress texture_mesh_b;
  glm::uint32_t alternator;

  // General sim timer
  glm::f32 tick;
};

struct Slime {
  // Used to traverse
  glm::vec2 position;
  // Storing the colour in each slime is silly, prefer a family index
  glm::uint32_t family_index;
  glm::f32 angle;
};

// Texture Mesh object
struct TextureColour {
  glm::uint8_t r;
  glm::uint8_t g;
  glm::uint8_t b;
  glm::uint8_t __padding__ = 0;
};

/*
 * Pipeline:
 * -> Compute Shader A takes the old mesh and:
 *  -> decreases the current cell colour by a fraction (say 10%)
 *  -> Averages the colour of the neighbouring cells
 *  -> Uploads this to the new mesh
 *
 * -> Compute Shader B interates over each slime and:
 *  -> Calculates its new position,
 *  -> Uploads its colour onto its position map in the mesh
 *  -> TODO: How are collisions handled?
 *
 * -> Graphics Pipeline
 */

struct SlimeCreateInfo {
  vk::ShaderStageFlagBits buffer_stage;
  GraphicsPipeline::PipelineContainerCreateInfo pipeline_details;
  vk::ClearColorValue default_colour;
  std::size_t max_frames_in_flight;
  ComputePipeline::ComputePipelineCreateInfo compute_details_texture;
  ComputePipeline::ComputePipelineCreateInfo compute_details_slime;

  uint32_t sim_width;
  uint32_t sim_height;
  uint32_t num_slimes;
};

struct SlimeApp : public VulkanAppInterface {
  static_assert(sizeof(SlimePushConstant) <= 128);
  static_assert(sizeof(Slime) == 16);
  static_assert(offsetof(Slime, position) == 0);
  static_assert(offsetof(Slime, family_index) == 8);
  static_assert(offsetof(Slime, angle) == 12);

public:
  // Vertices and Indices are defined inside it
  static auto create(SlimeCreateInfo info, VulkanRoot &root)
      -> std::expected<SlimeApp, std::string>;

  // Vulkan Interface requirements
  bool is_running() override;
  auto get_current_state(std::shared_ptr<GLFWwindow> window,
                         const VulkanAppRootRefs root_refs)
      -> std::expected<std::optional<VulkanAppTickState>, std::string> override;

  // Would be in the interface, perhaps might make its own separate interface
  // with generics if possible
  auto record_command_buffer(SlimePushConstant push_constants,
                             vk::Image &transition_image,
                             vk::raii::ImageView &image_view,
                             vk::Rect2D render_area, vk::Viewport viewport,
                             vk::Rect2D scissor) -> void;

  SlimeApp(GraphicsPipeline::PipelineContainer &&g_p_a,
           ComputePipeline::ComputePipelineContainer &&c_p_d_a,
           ComputePipeline::ComputePipelineContainer &&c_p_d_b,
           BufferUtils::CommandPoolAndBuffersContainer &&c_p_a_b,
           BufferUtils::DataBufferContainer<ImplementationHelp::FragApp::Vertex,
                                            uint16_t> &&d_b,
           SyncObjects::SyncObjectsContainer &&s_o,
           BufferUtils::ArbitraryGpuDataContainer<TextureColour> s_b_a,
           BufferUtils::ArbitraryGpuDataContainer<TextureColour> s_b_b,
           BufferUtils::ArbitraryGpuDataContainer<Slime> s, uint32_t m_f_i_f,
           vk::ClearColorValue d_c, std::pair<uint32_t, uint32_t> sim_size,
           uint32_t num_slimes)
      : graphics_pipeline_data(std::move(g_p_a)),
        compute_pipeline_data_mesh(std::move(c_p_d_a)),
        compute_pipeline_data_slime(std::move(c_p_d_b)),
        command_pool_and_buffers(std::move(c_p_a_b)),
        data_buffers(std::move(d_b)), sync_objects(std::move(s_o)),
        max_frames_in_flight(m_f_i_f), default_colour(d_c),
        sim_width(sim_size.first), sim_height(sim_size.second),
        num_slimes(num_slimes), texture_mesh_a(std::move(s_b_a)),
        texture_mesh_b(std::move(s_b_b)), slimes(std::move(s)) {}

private:
  // Vulkan wrappers
  GraphicsPipeline::PipelineContainer graphics_pipeline_data;
  ComputePipeline::ComputePipelineContainer compute_pipeline_data_mesh;
  ComputePipeline::ComputePipelineContainer compute_pipeline_data_slime;
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
  uint32_t num_slimes;

  BufferUtils::ArbitraryGpuDataContainer<TextureColour> texture_mesh_a;
  BufferUtils::ArbitraryGpuDataContainer<TextureColour> texture_mesh_b;
  BufferUtils::ArbitraryGpuDataContainer<Slime> slimes;

  uint32_t a_is_current = 0;
  float tick = 0;
};

auto create_slime_app(VulkanRoot &root) -> std::expected<SlimeApp, std::string>;

#endif
