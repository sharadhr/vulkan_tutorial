cmake_minimum_required(VERSION 3.24)
project(vulkan_tutorial)

include(cmake/compile_shaders.cmake)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_executable(vulkan_tutorial)

target_sources(vulkan_tutorial PRIVATE src/HelloTriangleApplication.cpp src/main.cpp)
target_glsl_shaders(vulkan_tutorial PRIVATE src/shaders/triangle.vert src/shaders/triangle.frag)
target_compile_features(vulkan_tutorial PRIVATE cxx_std_20)
set_target_properties(vulkan_tutorial PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(vulkan_tutorial PRIVATE glfw glm::glm fmt::fmt Threads::Threads Vulkan::Vulkan)
target_compile_definitions(vulkan_tutorial PRIVATE
        VULKAN_HPP_NO_CONSTRUCTORS
        VULKAN_HPP_NO_SMART_HANDLE
        VULKAN_HPP_STORAGE_SHARED
        VULKAN_HPP_STORAGE_SHARED_EXPORT
        VULKAN_HPP_LOADER_DYNAMIC_LOADER=1)
target_precompile_headers(vulkan_tutorial PRIVATE
        ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.hpp
        ${Vulkan_INCLUDE_DIR}/vulkan/vulkan_raii.hpp)
target_compile_options(vulkan_tutorial PRIVATE
        # windows and msvc/clang
        $<$<PLATFORM_ID:Windows>:/W4 /permissive- $<$<CXX_COMPILER_ID:Clang>:-Wno-braced-scalar-init>>
        # not-windows and clang or gcc
        $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<OR:$<CXX_COMPILER_ID:Clang,GNU>>>:-Wall -Wextra -pedantic -fsanitize=address -fsanitize=undefined -fdiagnostics-color=always -Wno-braced-scalar-init>)