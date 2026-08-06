
#include "glfw_window_handler.hh"
#include <iostream>

GlfwWindowContainer::GlfwWindowContainer(
    std::pair<uint32_t, uint32_t> dimensions, std::string window_name) {

  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // no opengl
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  window = std::shared_ptr<GLFWwindow>(
      glfwCreateWindow(dimensions.first, dimensions.second, window_name.c_str(),
                       nullptr, nullptr),
      [](GLFWwindow *w) { glfwDestroyWindow(w); });

  if (!window) {
    throw std::runtime_error("Failed to create GLFW window");
  } else {
    std::cout << "Established GLFW Window instance" << std::endl;
  }
}

GlfwWindowContainer::~GlfwWindowContainer() {
  std::cout << "Destroyed GLFW instance" << std::endl;
  glfwTerminate();
}

auto GlfwWindowContainer::get() -> std::weak_ptr<GLFWwindow> { return window; }
