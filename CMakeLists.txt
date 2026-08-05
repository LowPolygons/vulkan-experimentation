cmake_minimum_required(VERSION 3.28)

project(vulkan_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
#
# function (add_slang_shader_target TARGET)
#     cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
#     set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
#     set (ENTRY_POINTS -entry vertMain -entry fragMain)
#     add_custom_command (
#         OUTPUT ${SHADERS_DIR}
#         COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
#     )
#   add_custom_command (
#           OUTPUT  ${SHADERS_DIR}/slang.spv
#           COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
#           WORKING_DIRECTORY ${SHADERS_DIR}
#           DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
#           COMMENT "Compiling Slang Shaders"
#           VERBATIM
#   )
#   add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
# endfunction()
#
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
