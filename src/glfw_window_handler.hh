#ifndef VULKAN_TEST_GLFW_WINDOW_HANDLER_HH
#define VULKAN_TEST_GLFW_WINDOW_HANDLER_HH

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

/*
 * This is just a wrapper around the Window object, adding implicit smart
 * lifetime handling whilst also exposing access to the underlying pointer to
 * allow custom polling behaviour for example
 *
 * NOTE: This handles glfwInit and glfwTerminate internally, meaning it has an
 * application only in apps with exactly one window instance
 */
class GlfwWindowContainer {
public:
  GlfwWindowContainer(std::pair<uint32_t, uint32_t> dimensions,
                      std::string window_name);
  ~GlfwWindowContainer();

  auto get() -> std::weak_ptr<GLFWwindow>;

private:
  std::shared_ptr<GLFWwindow> window = nullptr;
};

#endif
