cmake_minimum_required(VERSION 3.24)
project(vulkan_tutorial)

include(cmake/compile_shaders.cmake)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)

add_executable(vulkan_tutorial)

target_sources(vulkan_tutorial PRIVATE src/HelloTriangleApplication.cpp src/main.cpp $<$<PLATFORM_ID:Linux>:src/dlclose.cpp>)
target_shaders(vulkan_tutorial GLSL PRIVATE src/shaders/triangle.vert src/shaders/triangle.frag)

target_compile_features(vulkan_tutorial PRIVATE cxx_std_20)
set_target_properties(vulkan_tutorial 
        PROPERTIES CXX_EXTENSIONS OFF
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
target_link_libraries(vulkan_tutorial PRIVATE glfw glm::glm fmt::fmt Threads::Threads Vulkan::Vulkan tinyobjloader::tinyobjloader)
target_compile_definitions(vulkan_tutorial PRIVATE
        VULKAN_HPP_NO_SMART_HANDLE
        VULKAN_HPP_STORAGE_SHARED
        VULKAN_HPP_STORAGE_SHARED_EXPORT
        VULKAN_HPP_LOADER_DYNAMIC_LOADER=1)
target_precompile_headers(vulkan_tutorial PRIVATE
        ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.hpp
        ${Vulkan_INCLUDE_DIR}/vulkan/vulkan_raii.hpp)
target_compile_options(vulkan_tutorial PRIVATE 
        # windows and msvc/clang
        $<$<PLATFORM_ID:Windows>:/W4 /permissive- $<$<CXX_COMPILER_ID:Clang>:-Wno-braced-scalar-init -ferror-limit=0>>
        # not-windows and clang or gcc
        $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<OR:$<CXX_COMPILER_ID:Clang,GNU>>>:-Wall -Wextra -pedantic -fsanitize=address,undefined -fdiagnostics-color=always -Wno-braced-scalar-init>)
target_link_options(vulkan_tutorial PRIVATE
        # not-windows and clang or gcc
        $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<OR:$<CXX_COMPILER_ID:Clang,GNU>>>:-fsanitize=address -fsanitize=undefined>)
