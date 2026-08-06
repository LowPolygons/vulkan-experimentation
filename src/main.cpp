#include "glfw_window_handler.hh"
#include "vulkan/vulkan.hpp"
#include <fstream>
#include <limits>

// This library provides a neat interface for mapping data from C++ to the slang
// shader
#include <glm/glm.hpp>
#include <tuple>

#ifndef VULKAN_APP_SHADER_PATH
constexpr bool SHADERS_NOT_FOUND = true;
#else
constexpr bool SHADERS_NOT_FOUND = false;
#endif

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm> // std::ranges::none_of
#include <cstdlib>
#include <cstring> // strcmp
#include <iostream>
#include <stdexcept>

struct ShaderVertex {
  glm::vec2 position;
  glm::vec3 colour;
  glm::f32 number;

  // Need to provide info on how to transfer this data to the gpu
  static vk::VertexInputBindingDescription get_binding_descriptions() {
    // Describes at which rate to load data from memory thrughout the vertices
    return {// Index of the binding in the arrya of bindings /shrug
            .binding = 0,
            // The number of bytes from one entry to the next
            .stride = sizeof(ShaderVertex),
            // eVertex moves to next data entry after each vertex, as opposed to
            // eInstance
            .inputRate = vk::VertexInputRate::eVertex};
  }

  // Next we need to describe how to actually handle the data inside of the
  // structure
  static std::array<vk::VertexInputAttributeDescription, 3>
  get_attribute_descriptions() {
    return {{vk::VertexInputAttributeDescription{
                 .location = 0,
                 .binding = 0,
                 .format = vk::Format::eR32G32Sfloat,
                 .offset = offsetof(ShaderVertex, position)},
             vk::VertexInputAttributeDescription{
                 .location = 1,
                 .binding = 0,
                 .format = vk::Format::eR32G32B32Sfloat,
                 .offset = offsetof(ShaderVertex, colour)},
             vk::VertexInputAttributeDescription{
                 .location = 2,
                 .binding = 0,
                 .format = vk::Format::eR32Sfloat,
                 .offset = offsetof(ShaderVertex, number)}}};
  }
};

const std::vector<ShaderVertex> vertices = {
    {{-0.5, -0.5}, {1.0, 1.0, 0.0}, 13.0},
    {{0.5, -0.5}, {1.0, 0.0, 1.0}, 12.0},
    {{0.5, 0.5}, {0.0, 1.0, 1.0}, 5.0},
    {{-0.5, 0.9}, {1.0, 1.0, 1.0}, 12.0},
};
const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

const std::vector<const char *> REQUIRED_DEVICE_EXTENSIONS = {
    vk::KHRSwapchainExtensionName};

const std::vector<char const *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
// constexpr bool ENABLE_VALIDATION_LAYERS = true;
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

// Before frames in flight, it would wait until the previous frame is finished
// before the next begins rendering
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

std::vector<char> read_shader(const std::string &file_name) {
  // End of file lets you immediately see the size of the file
  std::ifstream file(file_name, std::ios::ate | std::ios::binary);

  if (!file.is_open())
    throw std::runtime_error("Failed to open shader bytecode");

  std::vector<char> buffer(file.tellg());

  file.seekg(0, std::ios::beg);
  file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

  file.close();

  return buffer;
}

// TODO: better understand this function signature to allow a more modular
// wrapper
static VKAPI_ATTR vk::Bool32 VKAPI_CALL
callback_function(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                  vk::DebugUtilsMessageTypeFlagsEXT type,
                  const vk::DebugUtilsMessengerCallbackDataEXT *p_callback_data,
                  void *_p_user_data) {
  std::cerr << "validation layer: type " << to_string(type)
            << " msg: " << p_callback_data->pMessage << std::endl;

  return vk::False;
}

// NOTE: Any KHR just menas khronos

// TODO: Understand WHY each step has to be done and write it down
class HelloTriangleApplication {
public:
  HelloTriangleApplication(GlfwWindowContainer *window_container) {
    this->window_container = window_container;
  };

  void run() {
    initVulkan();
    mainLoop();
    wipeSwapChain();
  }

private:
  void initVulkan() {
    createInstance();
    createDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDeviceAndQueue();
    createSwapChain();
    createImageViews();
    createGraphicsPipeline();
    createCommandPool();
    createVertexBuffer();
    createIndexBuffer();
    createCommandBuffers();
    createSyncObjects();
  }

  // It isnt (of course) as simple as now allocating the memory, as different
  // GPUs may offer differnet types of memory to allocate
  auto find_memory_type(uint32_t type_filter,
                        vk::MemoryPropertyFlags property_flags) -> uint32_t {
    // device mem properties
    // Contains memoryTypes and memoryHeaps
    // A mem heap is a 'distinct memotry resource like dedicated vram and swap
    // space in ram for when vram runs out'
    vk::PhysicalDeviceMemoryProperties memory_props =
        physical_device.getMemoryProperties();
    // typefilter is a bit field of suitable memory types
    // this iterates over the bits and checks if the bit == 1
    //
    // we also need the data to be host visible and coherent for this usecase
    // these will be defiend in the property_flags
    for (auto i = 0; i < memory_props.memoryTypeCount; i++) {
      if ((type_filter & (1 << i)) &&
          (memory_props.memoryTypes[i].propertyFlags & property_flags) ==
              property_flags)
        return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
  }

  auto create_buffer(vk::DeviceSize buffer_create_info_size,
                     vk::BufferUsageFlags buffer_create_info_usage,
                     vk::MemoryPropertyFlags property_flags)
      -> std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> {

    auto buffer_info =
        vk::BufferCreateInfo{.size = buffer_create_info_size,
                             .usage = buffer_create_info_usage,
                             .sharingMode = vk::SharingMode::eExclusive};

    auto buffer = vk::raii::Buffer(device, buffer_info);
    auto memory_reqs = vk::MemoryRequirements{buffer.getMemoryRequirements()};
    auto memory_alloc_info =
        vk::MemoryAllocateInfo{.allocationSize = memory_reqs.size,
                               .memoryTypeIndex = find_memory_type(
                                   memory_reqs.memoryTypeBits, property_flags)};
    auto buffer_memory = vk::raii::DeviceMemory(device, memory_alloc_info);

    buffer.bindMemory(*buffer_memory, 0);

    return std::make_pair(std::move(buffer), std::move(buffer_memory));
  }

  void createIndexBuffer() {
    auto index_buffer_size = sizeof(uint16_t) * indices.size();

    auto [staging_buff, staging_mem] =
        create_buffer(index_buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

    void *mem_location = staging_mem.mapMemory(0, index_buffer_size);
    memcpy(mem_location, indices.data(),
           static_cast<std::size_t>(index_buffer_size));
    staging_mem.unmapMemory();

    std::tie(index_buffer, index_buffer_memory) =
        create_buffer(index_buffer_size,
                      vk::BufferUsageFlagBits::eIndexBuffer |
                          vk::BufferUsageFlagBits::eTransferDst,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(staging_buff, index_buffer, index_buffer_size);
  }

  // WARN: It is worth noting that you are not supposed to call allocateMemory
  // for every buffer individually, it should be done in bulk with multiple
  // objects, using the 'offset' parameter

  /*
   * To create the vertex buffer in a performant way, it creates two buffers:
   * a staging one which the CPU/host has access to, and a device only one,
   * both with identical sizes.
   *
   * It then performs a gpu-powered copy
   */
  void createVertexBuffer() {
    auto vertex_buffer_size = sizeof(ShaderVertex) * vertices.size();

    auto [ret_vertex_buffer, ret_vertex_buffer_memory] =
        create_buffer(vertex_buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

    // Map the cpu memory to the gpu
    void *memory_location =
        ret_vertex_buffer_memory.mapMemory(0, vertex_buffer_size);
    mempcpy(memory_location, vertices.data(), vertex_buffer_size);

    // Caching and other things may mean the data isnt immediately mapped to
    // memory, but eHostCoherent prevents that. There is a more performant way
    // to do this by mnanually flishng mapped memory ranges and invalivated them
    // before reading the mapped memory

    // TODO: understand why i have to map/unmap the memory
    ret_vertex_buffer_memory.unmapMemory();

    // unpacks the result directly into std::tie. In psuedo code its just
    //  (vertex_buffer, vertex_buffer_memory) = create_buffer()
    std::tie(vertex_buffer, vertex_buffer_memory) =
        create_buffer(vertex_buffer_size,
                      vk::BufferUsageFlagBits::eVertexBuffer |
                          vk::BufferUsageFlagBits::eTransferDst,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(ret_vertex_buffer, vertex_buffer, vertex_buffer_size);
  }

  void copyBuffer(vk::raii::Buffer &source_buffer,
                  vk::raii::Buffer &dest_buffer, vk::DeviceSize buffer_size) {
    auto allocation_info =
        vk::CommandBufferAllocateInfo{.commandPool = command_pool,
                                      .level = vk::CommandBufferLevel::ePrimary,
                                      .commandBufferCount = 1};
    auto command_copy_buffer = vk::raii::CommandBuffer{
        std::move(device.allocateCommandBuffers(allocation_info).front())};

    command_copy_buffer.begin(
        // Telling the driver the intention is good practise apparently
        {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // vk::BufferCopy is an array of regions to copy - the regions consist of a
    // source buffer offset, dest buffer offset and size
    command_copy_buffer.copyBuffer(*source_buffer, *dest_buffer,
                                   vk::BufferCopy(0, 0, buffer_size));
    command_copy_buffer.end();

    // This is a GPU op, so queue it and forcefully wait
    graphics_queue.submit(
        vk::SubmitInfo{.commandBufferCount = 1,
                       .pCommandBuffers = &*command_copy_buffer},
        nullptr);
    graphics_queue.waitIdle();

    // NOTE: if you're transfering multiple datas at once then using fences
    // gives the driver more optimisation opportunities
  }

  void wipeSwapChain() {
    swap_chain_image_views.clear();
    swap_chain = nullptr;
  }

  void recreateSwapChain() {
    // In the case that the window is minimised the frame buffer becomes 0 so
    // just pause to prevent wasted energy

    if (auto window = window_container->get().lock()) {
      int local_width = 0, local_height = 0;
      glfwGetFramebufferSize(window.get(), &local_width, &local_height);

      while (local_width == 0 || local_height == 0) {
        glfwGetFramebufferSize(window.get(), &local_width, &local_height);
        glfwWaitEvents();
      }
    } else {
      throw std::runtime_error("Could not receive window pointer");
    }

    device.waitIdle();

    wipeSwapChain();

    createSwapChain();
    createImageViews();
  }

  void mainLoop() {
    if (auto window = window_container->get().lock()) {
      while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        drawFrame();
      }
      device.waitIdle();
    } else {
      throw std::runtime_error(
          "Tried to utilise window after GlfwWindow deletion");
    }
  }

  void createDebugMessenger() {
#define MessageSeverity vk::DebugUtilsMessageSeverityFlagBitsEXT
#define MessageType vk::DebugUtilsMessageTypeFlagBitsEXT

    if (!ENABLE_VALIDATION_LAYERS)
      return;

    std::cout << "Reaching here" << std::endl;

    vk::DebugUtilsMessageSeverityFlagsEXT severity_flags(
        MessageSeverity::eWarning | MessageSeverity::eError);
    vk::DebugUtilsMessageTypeFlagsEXT message_type_flags(
        MessageType::eGeneral | MessageType::ePerformance |
        MessageType::eValidation);

    vk::DebugUtilsMessengerCreateInfoEXT debug_utils_messenger_info{
        .messageSeverity = severity_flags,
        .messageType = message_type_flags,
        // pointer to call back function
        .pfnUserCallback = &callback_function,
    };
    std::cout << "Reaching here" << std::endl;
    debug_messenger =
        instance.createDebugUtilsMessengerEXT(debug_utils_messenger_info);
    std::cout << "Reaching here" << std::endl;
  }

  void createSyncObjects() {
    assert(present_complete_semaphores.empty() &&
           render_finished_semaphores.empty() && draw_fences.empty());
    for (auto i = 0; i < swap_chain_images.size(); i++)
      render_finished_semaphores.emplace_back(device,
                                              vk::SemaphoreCreateInfo());

    for (auto i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      present_complete_semaphores.emplace_back(device,
                                               vk::SemaphoreCreateInfo());
      draw_fences.emplace_back(
          device,
          vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
  }

  // TODO: understand Semaphores & fences better
  void drawFrame() {
    // At a higher level:
    // -> Wait for previous frame to finish
    // -> Aquire image from swap chain
    // -> record command buffer which draws that scene into the image
    // -> submit the recorded command buffer
    // -> Present swap chain image

    // WARN: things here are async, so care is required
    // A binary 'Semaphore' is used to add order between async queue ops
    //
    // Semaphores act as a conductor:
    // Semaphore 'S' submits A and B, B depends on A
    // A signals to S it is finished, B waits until S signals to start to start
    //
    // Fences are similar, but used on the CPU

    // We have two synchro primitives and two scnearions to have
    // synchronisation: swapchain ops on the gpu, waitig for the previous frame
    // to finish

    // Takes array fo fences and waits on host for either any or all of the
    // fences to be signaled. vk::true means wait for all. the UINT64_MAX is a
    // timeout (we have essentially disabled it)
    auto fence_result = device.waitForFences(*draw_fences[current_frame_index],
                                             vk::True, UINT64_MAX);
    if (fence_result != vk::Result::eSuccess)
      throw std::runtime_error("Failed to wait for the draw fence");

    // First param is a timeout in nanoseconds, second & third is for synchro
    // objects
    // returns a vk::result, and the index of the swap chain image that is
    // available, referring to the vk::Image array
    auto [result, image_index] = swap_chain.acquireNextImage(
        UINT64_MAX, *present_complete_semaphores[current_frame_index], nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR) {
      recreateSwapChain();
      return;
    }
    if (result != vk::Result::eSuccess &&
        result != vk::Result::eSuboptimalKHR) {
      assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    device.resetFences(*draw_fences[current_frame_index]);

    // Record the commands to be submitted
    recordCommandBuffer(image_index);

    vk::PipelineStageFlags wait_destination_stage_mask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submit_info{
        // Firs three specify which semaphore to wait on BEFORE execution
        // begins, and where to wait in pipeline
        // We wait until the image is availabel to write colours
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*present_complete_semaphores[current_frame_index],
        .pWaitDstStageMask = &wait_destination_stage_mask,
        .commandBufferCount = 1,
        .pCommandBuffers = &*command_buffers[current_frame_index],
        .signalSemaphoreCount = 1,
        // Which semaphore to signal once it has finished execution
        .pSignalSemaphores = &*render_finished_semaphores[image_index]};

    graphics_queue.submit(submit_info, *draw_fences[current_frame_index]);

    // Early starbound swingStage flash backs :)
    current_frame_index = (current_frame_index + 1) % MAX_FRAMES_IN_FLIGHT;

    // WARN: MIssed section on subpass dependency which is "far more explicit
    // than is necessary" and thats saying something

    const vk::PresentInfoKHR present_info_khr{
        .waitSemaphoreCount = 1,
        // WHich to wait on
        .pWaitSemaphores = &*render_finished_semaphores[image_index],
        .swapchainCount = 1,
        // Swapchain to present the image to
        .pSwapchains = &*swap_chain,
        .pImageIndices = &image_index};

    result = graphics_queue.presentKHR(present_info_khr);

    if ((result == vk::Result::eSuboptimalKHR) ||
        (result == vk::Result::eErrorOutOfDateKHR)) {
      recreateSwapChain();
    } else {
      assert(result == vk::Result::eSuccess);
    }
  }

  void recordCommandBuffer(uint32_t image_index) {
    // This function writes the desired commands to the command buffer

    // It takes in a vk::CommandBufferBeginInfo structu
    command_buffers[current_frame_index].reset();
    command_buffers[current_frame_index].begin({});

    transition_image_layout(image_index, vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eColorAttachmentOptimal,
                            {}, // src access mask unused (dont need to wait for
                                // previous ops apparently lol)
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vk::ClearValue clear_colour = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachment_info{
        // which image to render
        .imageView = swap_chain_image_views[image_index],
        // layout the image will be in during rendering
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        // What to do with the image before rendering (clear to black)
        .loadOp = vk::AttachmentLoadOp::eClear,
        // What to do after (stores the image for us)
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear_colour};

    vk::RenderingInfo rendering_info{
        // sz/o render area, similar to render pass
        .renderArea = {.offset = {0, 0}, .extent = swap_chain_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment_info};

    command_buffers[current_frame_index].beginRendering(rendering_info);

    // first specify if it is a graphics or compute pipeline, and then the
    // instructions
    command_buffers[current_frame_index].bindPipeline(
        vk::PipelineBindPoint::eGraphics, *grapics_pipeline);

    command_buffers[current_frame_index].setViewport(
        0,
        vk::Viewport(0.0f, 0.0f, static_cast<float>(swap_chain_extent.width),
                     static_cast<float>(swap_chain_extent.height), 0.0f, 1.0f));
    command_buffers[current_frame_index].setScissor(
        0, vk::Rect2D(vk::Offset2D(0, 0), swap_chain_extent));

    // 0 -> offset into binding. the memory layout is exactly matching what we
    // want so its just 0
    // -> the array of buffers to bind
    // {0} -> an array of the same size of byte offsets to start reading vertex
    // data from TODO what does that mean
    command_buffers[current_frame_index].bindVertexBuffers(0, *vertex_buffer,
                                                           {0});
    // -> buffer, 0 -> offset, -> data type
    command_buffers[current_frame_index].bindIndexBuffer(
        *index_buffer, 0, vk::IndexType::eUint16);

    command_buffers[current_frame_index].drawIndexed(
        static_cast<uint32_t>(
            indices.size()), // indices count - how many vertices should be sent
                             // to the shader
        1, // instanceCount - instance rendering - 1 means dont do that
        0, // firstVirtext - offset into the index buffer -> defines the lowest
           // value of SV_VertexId
        0, // offset to add to the index buffer before indexing into the vertex
           // buffer
        0  // firstInstance - used as an ffset for isntanced rendering, defiens
           // the lowest value of SV_InstanceID
    );

    command_buffers[current_frame_index].endRendering();

    // Transition image to screen
    // WARN: NO IDEA
    transition_image_layout(image_index,
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::ImageLayout::ePresentSrcKHR,
                            vk::AccessFlagBits2::eColorAttachmentWrite, {},
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eBottomOfPipe);

    command_buffers[current_frame_index].end();
  }

  void createCommandBuffers() {
    if (device == nullptr)
      throw std::runtime_error(
          "Tried to create a graphics pipeline before device was initialised");
    // TODO: no clue
    vk::CommandBufferAllocateInfo alloc_info{
        .commandPool = command_pool,
        // Can be subimitted to queue for execution, but not called from other
        // command buffers
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT};

    command_buffers = vk::raii::CommandBuffers(device, alloc_info);
  }

  // WARN: copied directly from
  // The idea is that images are generic to be specialisable fro different
  // things, such as for presentation to the screen or for color attachements
  // etc
  //
  // This function transitions the iamge layout from ::UNdefined to
  // ::eColorAttachementOptimal
  // https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/01_Command_buffers.html
  void transition_image_layout(uint32_t imageIndex, vk::ImageLayout old_layout,
                               vk::ImageLayout new_layout,
                               vk::AccessFlags2 src_access_mask,
                               vk::AccessFlags2 dst_access_mask,
                               vk::PipelineStageFlags2 src_stage_mask,
                               vk::PipelineStageFlags2 dst_stage_mask) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swap_chain_images[imageIndex],
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    vk::DependencyInfo dependency_info = {.dependencyFlags = {},
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &barrier};
    command_buffers[current_frame_index].pipelineBarrier2(dependency_info);
  }

  void createCommandPool() {
    if (device == nullptr)
      throw std::runtime_error(
          "Tried to create a graphics pipeline before device was initialised");

    vk::CommandPoolCreateInfo pool_info{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queue_index};

    command_pool = vk::raii::CommandPool(device, pool_info);
  }

  /*
   * Roughly this is what happens in the graphics pipeline:
   *
   * - Given the raw vertex data from the buffers:
   *
   *   Input assembler: collects this data, performs operations like repeating
   * certain elements where necessary without duplicating the vertex data itself
   *
   *   Vertex Shader: run for every vertex - applis transformations to turn it
   * from model space to screen space. Also, per-vertex data is passed down the
   * pipeline
   *
   *   Tessellation shaders: can let you subdivide geomtry based on rules to
   * increase mesh quality. This is used, for example, to make faces like a
   * brick wall less flat when nearby
   *
   *   Geometry shader: this is run on every triangle, line or point
   * (primitives) and can discord it, or output more primitives than came in. It
   * is similar to the tesselation shader but more flexible. It is used LITTLE
   * nowadays due to performance issues except for integrated intel gpus. Almost
   * all can be fixed with a modern mesh shader pipeline. This is an entirely
   * different style of pipeline, like how raytracing is
   *
   *   Rasterization: this breaks the primitives into fragments. These are the
   * actual pixel elements which appear on the framebuffer. As a result,
   * fragments outside the screen are discarded. Depth testing also happens,
   * discarding fragements behind others
   *
   *   Fragment shader: invoked for every fragment left. It detemines which
   * framebuffer the fragments are written to, and with what color and depth
   * values. This can be done with interpolated data from the vertex shader,
   * which can include texture coordinates, lighting normals
   *
   *   Colour blending: applies operations to mix different fragments which map
   * to the same pixel in the framebuffer. Fragments will overwrite, add or be
   * mixed based on transparency
   *
   * NOTE: Some of these are configurable via arguments but have fixed internal
   * logic
   * NOTE: Some of these are programmable
   *
   * INPUT ASSEMBLER, RASTERIZATION, COLOUR BLENDING - Configurable but Fixed
   *
   * VERTEX SHADER, TESSELLATION, GEOMETRY SHADER, FRAGMENT SHADER -
   * Programmable
   *
   * Some of the programmable stages are optional. Tesselation and geometry can
   * be disabled if you just draw simple geometry. If you only want depth, you
   * can disable fragment shaders which is useful for shadow map generation
   */
  void createGraphicsPipeline() {
    if (device == nullptr)
      throw std::runtime_error(
          "Tried to create a graphics pipeline before device was initialised");

    auto shader_bytecode = read_shader("shaders/slang.spv");
    vk::raii::ShaderModule shader_module = createShaderModule(shader_bytecode);

    // The use the shader, the pipeline stages need to be assigned via a
    // structure
    vk::PipelineShaderStageCreateInfo vertex_shader_stage_info{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shader_module,
        .pName = "vertMain"};
    vk::PipelineShaderStageCreateInfo frag_shader_stage_info{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shader_module,
        .pName = "fragMain"};
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = {
        vertex_shader_stage_info, frag_shader_stage_info};

    // Describes the format of the vertex data passed to the shader:
    // -> Bindings: spacing between data and whether the data is per-vertex or
    // instance
    // -> Attribute descriptions: type of the attributes passed to the vertex
    // shader, which bindinf to load them from and at which offset

    std::vector<vk::DynamicState> dynamic_states = {vk::DynamicState::eViewport,
                                                    vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state{
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data()};

    // Get descriptors of the data that will be passed in
    auto binding_description = ShaderVertex::get_binding_descriptions();
    auto attribute_description = ShaderVertex::get_attribute_descriptions();
    vk::PipelineVertexInputStateCreateInfo vertex_input_info{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attribute_description.size()),
        .pVertexAttributeDescriptions = attribute_description.data()};

    // INPUT ASSEMBLY
    // WARN: ASSUMPTION: the viewport will ALWAYS be the full screen span
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        // Triangle lists only so far
        .topology = vk::PrimitiveTopology::eTriangleList};

    // What region of the screen will be rednered too
    vk::Viewport viewport{0.0f,
                          0.0f,
                          static_cast<float>(swap_chain_extent.width),
                          static_cast<float>(swap_chain_extent.height),
                          0.0f,  // Min depth
                          1.0f}; // Max depth
    // Within the framebuffer, how much of it should appear in the final image
    // Eg: if you do a half height scissor on a half height viewport, only the
    // top quarter of the screen will appear
    vk::Rect2D scissor{vk::Offset2D{0, 0}, swap_chain_extent};

    // TODO: implement dynamic scissor and viewport
    // https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/02_Fixed_functions.html

    vk::PipelineViewportStateCreateInfo viewport_state{.viewportCount = 1,
                                                       .pViewports = &viewport,
                                                       .scissorCount = 1,
                                                       .pScissors = &scissor};

    // Rasterizer - vertices to fragments
    vk::PipelineRasterizationStateCreateInfo rasterizer_info{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        // NOTE: This is useful (wireframes accessible here)
        .polygonMode = vk::PolygonMode::eFill,
        // NOTE: Potentially interesting
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        // NOTE: Also useful
        // WARN: anything thicker than 1 requires wideLines gpu feature
        .lineWidth = 1.0f};

    // Multi-sampling -> combining fragment shaders results of multiply polygons
    // which rasterize to teh saem pixel (anti-aliesing)
    // INFO: for now, false
    vk::PipelineMultisampleStateCreateInfo multi_sampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
    };

    // TODO: to revisit
    vk::PipelineDepthStencilStateCreateInfo *depth_stencil_buffer_info =
        nullptr;

    // Colour blending - after a frag shader retunrs the colour, how should ti
    // be combined with the framebuffer colour
    //
    // INFO TODO: it would seem that this is very variable and perhaps best to
    // be handed in
    vk::PipelineColorBlendAttachmentState colour_blend_attachment{
        .blendEnable = vk::False,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    vk::PipelineColorBlendStateCreateInfo colour_blending{
        // Subject to variability depending on method blending. perhaps THIS
        // should be passed in
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colour_blend_attachment};

    // Update the pipeline layout
    vk::PipelineLayoutCreateInfo pipeline_layout_info{
        .setLayoutCount = 0, .pushConstantRangeCount = 0};

    pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

    // DYNAMIC RENDERING
    // eed to specify the formats of the attachments that will be using during
    // rendering
    vk::PipelineRenderingCreateInfo pipeline_rendering_info{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swap_chain_surface_format.format};

    // TODO: Christ
    // https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/04_Conclusion.html
    vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                       vk::PipelineRenderingCreateInfo>
        pipeline_info_chain = {
            vk::GraphicsPipelineCreateInfo{
                .stageCount = 2,
                .pStages = shader_stages.data(),
                .pVertexInputState = &vertex_input_info,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterizer_info,
                .pMultisampleState = &multi_sampling,
                .pColorBlendState = &colour_blending,
                .pDynamicState = &dynamic_state,
                .layout = pipeline_layout,
                .renderPass = nullptr},
            vk::PipelineRenderingCreateInfo{
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &swap_chain_surface_format.format}};

    grapics_pipeline = vk::raii::Pipeline(
        device, nullptr, // this nullptr is pipelinecache
        pipeline_info_chain.get<vk::GraphicsPipelineCreateInfo>());
  }

  [[nodiscard]] vk::raii::ShaderModule
  createShaderModule(const std::vector<char> &code) const {
    if (device == nullptr)
      throw std::runtime_error(
          "Tried to create a shader module before device was initialised");

    vk::ShaderModuleCreateInfo create_info{
        .codeSize = code.size() * sizeof(char),
        // THe size fo the bytecode is specified in bytes, but the bytecode
        // pointer is a uint32_t so you reinterpret cast
        .pCode = reinterpret_cast<const uint32_t *>(code.data())};

    return vk::raii::ShaderModule(device, create_info);
  }

  void createSurface() {
    // There is some stuff in here that only works this simplistically because
    // glfw interfaces so well with vulkan check
    // https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/00_Window_surface.html
    // for more details

    if (GLFWwindow *window = window_container->get().lock().get()) {
      VkSurfaceKHR local_surface;
      if (glfwCreateWindowSurface(*instance, window, nullptr, &local_surface) !=
          0) {
        throw std::runtime_error("Failed to create a window surface");
      }
      surface = vk::raii::SurfaceKHR(instance, local_surface);
    }
  }

  /*
   * Need a view onto each of the swap chain images
   */
  void createImageViews() {
    assert(swap_chain_image_views.empty());
    // TODO: a lot of the data is ambiguous
    vk::ImageViewCreateInfo image_view_create_info{
        .viewType = vk::ImageViewType::e2D,
        .format = swap_chain_surface_format
                      .format, // How the colour space components are configured
        // This describes what the images purpose is and which part of the image
        // should be accessed
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};

    for (auto &image : swap_chain_images) {
      image_view_create_info.image = image;
      swap_chain_image_views.emplace_back(device, image_view_create_info);
    }
  }

  /*
   * Swap chain is a list of the surfaces that the OS returns to vulkan to be
   * drawn upon - i think
   * TODO: learn more
   */
  void createSwapChain() {
    if (instance == nullptr || physical_device == nullptr ||
        device == nullptr || graphics_queue == nullptr || surface == nullptr)
      throw std::runtime_error("Instance or physical device not initialised "
                               "before attempts to use it");

    // The physical device contains info on basic surface capabilities such as
    // min/max images in the swapchain, min/max width and height of images
    auto surface_capabilities =
        physical_device.getSurfaceCapabilitiesKHR(*surface);
    // Also the available surface formats (pixel format, colour space)
    std::vector<vk::SurfaceFormatKHR> available_formats =
        physical_device.getSurfaceFormatsKHR(*surface);
    // Also presentation modes
    std::vector<vk::PresentModeKHR> available_present_modes =
        physical_device.getSurfacePresentModesKHR(*surface);

    // If these different settings, there may be ones that are more optimal than
    // others
    //
    // For surface format: colour depth
    // Presentation mode: conditions for swapping images to the screen
    // swap extent: resolution of images in the swapchain
    //
    // For each, there is an ideal value in mind
    assert(!available_formats.empty());
    bool format_was_chosen = false;
    vk::SurfaceFormatKHR chosen_swap_surface_format = available_formats[0];

    // WARN: strictly speaking, this code should look for altermative methods,
    // but im just going to say if it doesnt support BGRA then crash
    for (vk::SurfaceFormatKHR surf_form : available_formats) {
      if (surf_form.format == vk::Format::eB8G8R8A8Srgb &&
          surf_form.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
        chosen_swap_surface_format = surf_form;
        format_was_chosen = true;
        break;
      }
    }

    if (!format_was_chosen)
      throw std::runtime_error(
          "Device swap chain surface format doesnt support eB8G8R8A8Srgb");

    // WARN: DEV: IF THERE IS SCREEN TEARING THEN IT IS BECUASE HTIS
    // AUTOMATICALLY CHOOSES THE ONLY GUARANTEED PRESENT MODE
    vk::PresentModeKHR chosen_present_mode = vk::PresentModeKHR::eFifo;

    // NOTE: for calculating the swap extent, if its current extent isnt the
    // uint32 max, then its swap extend is just the screen size
    std::pair<int, int> extent = {-1, -1};

    if (surface_capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
      extent = {static_cast<int>(surface_capabilities.currentExtent.width),
                static_cast<int>(surface_capabilities.currentExtent.height)};
    } else {
      int width, height;
      if (GLFWwindow *window = window_container->get().lock().get()) {
        glfwGetFramebufferSize(window, &width, &height);

        // Clamping between min and max, then casting
        extent.first = static_cast<int>(std::clamp<uint32_t>(
            width, surface_capabilities.minImageExtent.width,
            surface_capabilities.maxImageExtent.width));
        extent.second = static_cast<int>(std::clamp<uint32_t>(
            height, surface_capabilities.minImageExtent.height,
            surface_capabilities.maxImageExtent.height));
      }
    }

    if (extent.first < 0 || extent.second < 0)
      throw std::runtime_error("Swap Extent couldn't be assigned");

    vk::Extent2D chosen_swap_extent = {static_cast<uint32_t>(extent.first),
                                       static_cast<uint32_t>(extent.second)};

    // Relevant values:
    // surface_capabilities
    // chosen_swap_surface_format
    // chosen_present_mode
    // chosen_swap_extent

    uint32_t swap_chain_image_count =
        (surface_capabilities.maxImageCount == 0)
            ? surface_capabilities.maxImageCount
            : std::clamp<uint32_t>(surface_capabilities.maxImageCount + 1,
                                   surface_capabilities.minImageCount,
                                   surface_capabilities.maxImageCount);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *surface,
        .minImageCount = swap_chain_image_count,
        .imageFormat = chosen_swap_surface_format.format,
        .imageColorSpace = chosen_swap_surface_format.colorSpace,
        .imageExtent = chosen_swap_extent,
        .imageArrayLayers = 1,
        // This may change if you're, for exmaple, going to do post processing
        // on an image
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = chosen_present_mode,
        .clipped = true};

    swap_chain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
    swap_chain_images = swap_chain.getImages();
    swap_chain_surface_format = chosen_swap_surface_format;
    swap_chain_extent = chosen_swap_extent;

    auto swap_chain_info = swap_chain.getDevice();

    std::cout << "Swap chain device is " << swap_chain_info << std::endl;
  }

  void createLogicalDeviceAndQueue() {
    // First, you select the list of queues you're interested in via their
    // properties.
    // for this, we only care about graphics capabilities
    if (instance == nullptr || physical_device == nullptr)
      throw std::runtime_error("Instance or physical device not initialised "
                               "before attempts to use it");

    // TODO: redo in a way that makes sense
    std::vector<vk::QueueFamilyProperties> queue_fam_properties =
        physical_device.getQueueFamilyProperties();

    // Apparently thats a bitwise NOT
    queue_index = ~0;

    // TODO: understand what is happening here
    for (uint32_t q_fam_prop_index = 0;
         q_fam_prop_index < queue_fam_properties.size(); q_fam_prop_index++) {
      if ((queue_fam_properties[q_fam_prop_index].queueFlags &
           vk::QueueFlagBits::eGraphics) &&
          physical_device.getSurfaceSupportKHR(q_fam_prop_index, *surface)) {
        queue_index = q_fam_prop_index;
        break;
      }
    }

    // DEVICE FEATURES
    vk::PhysicalDeviceFeatures device_features;

    //  For more modern vulkan features, you must explicity request them
    //  (anything > 1.0)
    // The chosen solution for implementing multiply features is through a
    // structure chain which can point to another structure
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {}, // physical device empty for now
            {.shaderDrawParameters =
                 true}, // Only choosing this feature from vulkan 11, etc
            {.synchronization2 = true, .dynamicRendering = true},
            {.extendedDynamicState = true}};

    // Queue Priority (requried even if there is one queue)
    float queuePriority = 0.5;
    // The struct for specifying the number of queues for a single queue
    // family
    vk::DeviceQueueCreateInfo device_queue_create_info{
        .queueFamilyIndex = queue_index,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority};
    // With all the info gathered, and the required device extensions, create
    // the logical device
    vk::DeviceCreateInfo device_create_info{
        .pNext = &featureChain.get<
            vk::PhysicalDeviceFeatures2>(), // Reference to the first structure
                                            // in the chain rather than the
                                            // chain itself
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_create_info,
        .enabledExtensionCount =
            static_cast<uint32_t>(REQUIRED_DEVICE_EXTENSIONS.size()),
        .ppEnabledExtensionNames = REQUIRED_DEVICE_EXTENSIONS.data(),
    };

    device = vk::raii::Device(physical_device, device_create_info);
    graphics_queue = vk::raii::Queue(device, queue_index, 0);
  }

  /*
   * This function will find all possible devices that can run vulkan
   *
   * it will:
   *
   * - try choose a discrete gpu (not integrated graphics)
   *
   * - confirm the gpu supports the right api version
   * - confirm the gpu supports the right command queues
   * - confirm the gpu supports the needed extensions
   * - confirm the gpu has the right features
   */
  void pickPhysicalDevice() {
    if (instance == nullptr)
      throw std::runtime_error(
          "VK Instance has not been initialised before trying to use it");

    auto all_physical_devices = instance.enumeratePhysicalDevices();

    if (all_physical_devices.empty())
      throw std::runtime_error(
          "There are no physical devices with vulkan support");

    for (auto _physical_device : all_physical_devices) {
      auto device_properties = _physical_device.getProperties();
      auto device_features = _physical_device.getFeatures();

      // TODO: this function can be a lot more intelligent, ranking gpus and
      // such. For now, this will just pick the first DISCRETE GPU
      if (device_properties.deviceType ==
              vk::PhysicalDeviceType::eDiscreteGpu &&
          device_features.geometryShader) {
        this->physical_device = _physical_device;
        break;
      }
    }

    if (!(physical_device.getProperties().apiVersion >= vk::ApiVersion13))
      throw std::runtime_error("Gpu API version does not meet api version");

    // Queue Stuff now
    /*
     * Every operation must be submitted to a queue, and there are different
     * queue families. Each family will only allow a certain subset of commands.
     *
     * For this context, we need to check that the devices supported queues
     * include those that support the desired properties
     */

    auto queue_families = physical_device.getQueueFamilyProperties();

    if (!(std::ranges::any_of(queue_families, [](auto const &q_fam_props) {
          return static_cast<bool>(q_fam_props.queueFlags &
                                   vk::QueueFlagBits::eGraphics);
        })))
      throw std::runtime_error(
          "Device does not support graphics-oriented queues");

    // Similarly, we alsoneed to ensure it supports the right extensions
    auto available_device_extensions =
        physical_device.enumerateDeviceExtensionProperties();

    // TODO: redo in a way that makes sense
    if (!(std::ranges::all_of(
            REQUIRED_DEVICE_EXTENSIONS,
            [&available_device_extensions](auto const &required_deve_ext) {
              return std::ranges::any_of(
                  available_device_extensions,
                  [required_deve_ext](auto const &avail_dev_ext) {
                    return strcmp(avail_dev_ext.extensionName,
                                  required_deve_ext) == 0;
                  });
            })))
      throw std::runtime_error(
          "The device does not support the right extensions");

    // TODO: redo in a way that makes sense
    // Finally, confirm that it supports all the right features
    auto device_features = physical_device.template getFeatures2<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    // clang-format off
    if (!(
          device_features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
          device_features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
          device_features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState
         ))
      throw std::runtime_error("The device does not support the right features");
    // clang-format on
  }

  /*
   * So far:
   * - Gets app information
   *
   * - Gets required validation layers - a validation layer is an optional
   * component 'that hook into vulkan function calls to apply additional
   * operations'
   *
   * - Gets the required extensions from GLFW - the extensions specify how to
   * interface the vulkan driver with the window system. GLFW provides this
   *
   * - Finally creates the vulkan instance
   */
  void createInstance() {
    vk::ApplicationInfo appInfo{.pApplicationName = "Hello World Triangle",
                                .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                .pEngineName = "No Engine",
                                .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                .apiVersion = vk::ApiVersion13};

    // Get required layers
    std::vector<char const *> requiredLayers;
    if (ENABLE_VALIDATION_LAYERS) {
      requiredLayers.assign(validationLayers.begin(), validationLayers.end());
    }
    // Check if the required layers are supported by the existing vulkan
    // implementation
    auto layerProperties = context.enumerateInstanceLayerProperties();
    // TODO: better understand what this is doing
    auto unsupportedLayerIterator = std::ranges::find_if(
        requiredLayers, [&layerProperties](auto const &requiredLayer) {
          return std::ranges::none_of(
              layerProperties, [requiredLayer](auto const &layerProperty) {
                return strcmp(layerProperty.layerName, requiredLayer) == 0;
              });
        });

    if (unsupportedLayerIterator != requiredLayers.end())
      throw std::runtime_error("Required layer is not supported: " +
                               std::string(*unsupportedLayerIterator));

    // Get the required instance extensions from GLFW
    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector all_extensions(glfwExtensions,
                               glfwExtensions + glfwExtensionCount);

    if (ENABLE_VALIDATION_LAYERS)
      all_extensions.push_back(vk::EXTDebugUtilsExtensionName);

    for (auto e : all_extensions)
      std::cout << "Enabled extensions: " << e << std::endl;

    // Check if the required FLGW exensions are supported by the vulkan
    // implementation
    auto extensionProperties = context.enumerateInstanceExtensionProperties();

    for (uint32_t i = 0; i < all_extensions.size(); ++i) {
      if (std::ranges::none_of(extensionProperties,
                               [curr_extension = all_extensions[i]](
                                   auto const &extensionProperty) {
                                 return strcmp(extensionProperty.extensionName,
                                               curr_extension) == 0;
                               })) {
        throw std::runtime_error("Required extension not supported: " +
                                 std::string(all_extensions[i]));
      }
    }
    // Application Info
    // Also need the layers we need access to
    // Also need the global extensions we need
    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(all_extensions.size()),
        .ppEnabledExtensionNames = all_extensions.data(),
    };

    instance = vk::raii::Instance(context, createInfo);
  }

private:
  GlfwWindowContainer *window_container;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;
  vk::raii::SurfaceKHR surface = nullptr;
  vk::raii::PhysicalDevice physical_device = nullptr;
  vk::raii::Device device = nullptr;
  uint32_t queue_index;
  vk::raii::Queue graphics_queue = nullptr;
  vk::raii::SwapchainKHR swap_chain = nullptr;
  std::vector<vk::Image> swap_chain_images;
  vk::SurfaceFormatKHR swap_chain_surface_format;
  vk::Extent2D swap_chain_extent;
  std::vector<vk::raii::ImageView> swap_chain_image_views;
  // Useful for specifying push contstants too
  vk::raii::PipelineLayout pipeline_layout = nullptr;
  vk::raii::Pipeline grapics_pipeline = nullptr;
  vk::raii::CommandPool command_pool = nullptr;

  vk::raii::Buffer vertex_buffer = nullptr;
  vk::raii::DeviceMemory vertex_buffer_memory = nullptr;
  vk::raii::Buffer index_buffer = nullptr;
  vk::raii::DeviceMemory index_buffer_memory = nullptr;

  std::vector<vk::raii::CommandBuffer> command_buffers;
  std::vector<vk::raii::Semaphore> present_complete_semaphores;
  std::vector<vk::raii::Semaphore> render_finished_semaphores;
  std::vector<vk::raii::Fence> draw_fences;
  uint32_t current_frame_index = 0;
};

int main() {
  constexpr uint32_t WIDTH = 800;
  constexpr uint32_t HEIGHT = 600;

  if (SHADERS_NOT_FOUND)
    throw std::runtime_error("Could not find shaders directory");

  try {
    GlfwWindowContainer container({WIDTH, HEIGHT}, "Test GLFW Window");
    HelloTriangleApplication app(&container);

    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
