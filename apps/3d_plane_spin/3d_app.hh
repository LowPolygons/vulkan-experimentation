#include "vulkan_wrapper/buffers/command_buffer_container.hh"
#include "vulkan_wrapper/buffers/data_buffer_container.hh"
#include "vulkan_wrapper/buffers/uniform_buffer_container.hh"
#include "vulkan_wrapper/pipeline/graphics_pipeline_container.hh"
#include "vulkan_wrapper/syncs/sync_object_container.hh"
#include "vulkan_wrapper/wrapper_boilerplate.hh"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/fwd.hpp>
#include <vulkan/vulkan_raii.hpp>

struct ShaderVertex3D {
  glm::vec2 position;
  glm::vec3 colour;
  glm::f32 number;
  static vk::VertexInputBindingDescription get_binding_descriptions() {
    return {// Index of the binding in the arrya of bindings /shrug
            .binding = 0,
            // The number of bytes from one entry to the next
            .stride = sizeof(ShaderVertex3D),
            // eVertex moves to next data entry after each vertex, as opposed to
            // eInstance
            .inputRate = vk::VertexInputRate::eVertex};
  }
  static std::vector<vk::VertexInputAttributeDescription>
  get_attribute_descriptions() {
    return {{vk::VertexInputAttributeDescription{
                 .location = 0,
                 .binding = 0,
                 .format = vk::Format::eR32G32Sfloat,
                 .offset = offsetof(ShaderVertex3D, position)},
             vk::VertexInputAttributeDescription{
                 .location = 1,
                 .binding = 0,
                 .format = vk::Format::eR32G32B32Sfloat,
                 .offset = offsetof(ShaderVertex3D, colour)},
             vk::VertexInputAttributeDescription{
                 .location = 2,
                 .binding = 0,
                 .format = vk::Format::eR32Sfloat,
                 .offset = offsetof(ShaderVertex3D, number)}}};
  }
};

struct App3DCreateInfo {
  vk::ShaderStageFlagBits buffer_stage;
  GraphicsPipeline::PipelineContainerCreateInfo pipeline_details;
  vk::ClearColorValue default_colour;

  // WARN: This may be a VulkanRoot piece of data
  std::size_t max_frames_in_flight;
};

struct App3DUniformBuffer {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

struct App3D : public VulkanAppInterface {
  static auto create(App3DCreateInfo info, VulkanRoot &root,
                     std::vector<ShaderVertex3D> vertices,
                     std::vector<uint16_t> indices)
      -> std::expected<App3D, std::string>;

  bool is_running() override;
  auto get_current_state(std::shared_ptr<GLFWwindow> window,
                         const VulkanAppRootRefs root_refs)
      -> std::expected<std::optional<VulkanAppTickState>, std::string> override;

  auto record_command_buffer(vk::Image &transition_image,
                             vk::raii::ImageView &image_view,
                             vk::Rect2D render_area, vk::Viewport viewport,
                             vk::Rect2D scissor) -> void;

  auto update_uniform_buffer(uint32_t current_image, vk::Extent2D &dimensions)
      -> void;

private:
  App3D(BufferUtils::UniformBufferContainer<App3DUniformBuffer>
            &&uniform_buffer_container,
        GraphicsPipeline::PipelineContainer &&p_d,
        BufferUtils::CommandPoolAndBuffersContainer &&c_p_a_b,
        BufferUtils::DataBufferContainer<ShaderVertex3D, uint16_t> &&d_b,
        SyncObjects::SyncObjectsContainer &&s_o, uint32_t m_f_i_f,
        vk::ClearColorValue d_c)
      : uniform_buffer_container(std::move(uniform_buffer_container)),
        pipeline_data(std::move(p_d)),
        command_pool_and_buffers(std::move(c_p_a_b)),
        data_buffers(std::move(d_b)), sync_objects(std::move(s_o)),
        max_frames_in_flight(m_f_i_f), default_colour(d_c) {};

public:
  BufferUtils::UniformBufferContainer<App3DUniformBuffer>
      uniform_buffer_container;
  GraphicsPipeline::PipelineContainer pipeline_data;
  BufferUtils::CommandPoolAndBuffersContainer command_pool_and_buffers;
  BufferUtils::DataBufferContainer<ShaderVertex3D, uint16_t> data_buffers;

  SyncObjects::SyncObjectsContainer sync_objects;

  uint32_t max_frames_in_flight;
  uint32_t current_frame_index = 0;

  vk::ClearColorValue default_colour;
};

auto create_3d_app(VulkanRoot &vulkan_root)
    -> std::expected<App3D, std::string>;
