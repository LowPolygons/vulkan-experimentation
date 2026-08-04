#include "glfw_window_handler.hh"
#include "vulkan/vulkan.hpp"
#include <limits>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm> // std::ranges::none_of
#include <cstdlib>
#include <cstring> // strcmp
#include <iostream>
#include <stdexcept>

const std::vector<const char *> REQUIRED_DEVICE_EXTENSIONS = {
    vk::KHRSwapchainExtensionName};

const std::vector<char const *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

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
  }

private:
  void initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDeviceAndQueue();
    createSwapChain();
  }

  void mainLoop() {
    if (auto window = window_container->get().lock()) {
      while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
      }
    } else {
      throw std::runtime_error(
          "Tried to utilise window after GlfwWindow deletion");
    }
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
    uint32_t queue_index = ~0;

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
            {.dynamicRendering = true},
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
    if (ENABLE_VALIDATION_LAYERS)
      requiredLayers.assign(validationLayers.begin(), validationLayers.end());

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

    // Check if the required FLGW exensions are supported by the vulkan
    // implementation
    auto extensionProperties = context.enumerateInstanceExtensionProperties();

    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
      if (std::ranges::none_of(extensionProperties,
                               [glfwExtension = glfwExtensions[i]](
                                   auto const &extensionProperty) {
                                 return strcmp(extensionProperty.extensionName,
                                               glfwExtension) == 0;
                               })) {
        throw std::runtime_error("Required GLFW extension not supported: " +
                                 std::string(glfwExtensions[i]));
      }
    }
    // Application Info
    // Also need the layers we need access to
    // Also need the global extensions we need
    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = glfwExtensionCount,
        .ppEnabledExtensionNames = glfwExtensions,
    };

    instance = vk::raii::Instance(context, createInfo);
  }

private:
  GlfwWindowContainer *window_container;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  vk::raii::SurfaceKHR surface = nullptr;
  vk::raii::PhysicalDevice physical_device = nullptr;
  vk::raii::Device device = nullptr;
  vk::raii::Queue graphics_queue = nullptr;
  vk::raii::SwapchainKHR swap_chain = nullptr;
  std::vector<vk::Image> swap_chain_images;
};

int main() {
  constexpr uint32_t WIDTH = 800;
  constexpr uint32_t HEIGHT = 600;

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
