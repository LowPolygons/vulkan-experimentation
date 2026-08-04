cmake_minimum_required(VERSION 3.28)

project(vulkan_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

add_executable(vulkan_app
    src/glfw_window_handler.cc
    src/main.cpp
)

target_link_libraries(vulkan_app PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

target_compile_definitions(vulkan_app PRIVATE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)
