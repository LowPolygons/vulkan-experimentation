cmake_minimum_required(VERSION 3.28)

project(vulkan_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SHADER_SLANG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader.slang
)
message(STATUS "Shader sources: ${SHADER_SLANG_SOURCES}")

find_program(SLANGC_EXECUTABLE slangc REQUIRED)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

function (add_slang_shader_target TARGET)
    # thank god this was written for me by khronos because what is going on here
    cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
    set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
    set (ENTRY_POINTS -entry vertMain -entry fragMain)
    add_custom_command (
        OUTPUT ${SHADERS_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
    )
  add_custom_command (
          OUTPUT  ${SHADERS_DIR}/slang.spv
          COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
          WORKING_DIRECTORY ${SHADERS_DIR}
          DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
          COMMENT "Compiling Slang Shaders"
          VERBATIM
  )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()

add_executable(vulkan_app
    src/glfw_window_handler.cc
    src/main.cpp
)


add_slang_shader_target( shaders SOURCES ${SHADER_SLANG_SOURCES})
add_dependencies(vulkan_app 
    shaders
)

target_link_libraries(vulkan_app PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

target_compile_definitions(vulkan_app PRIVATE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
    # IMPERATIVE if you want to be able to resize without crashing
    VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
)
target_compile_definitions(vulkan_app PRIVATE
    VULKAN_APP_SHADER_PATH="${CMAKE_CURRENT_LIST_DIR}/shaders"
)
