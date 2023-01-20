cmake_minimum_required(VERSION 3.24)
project(vulkan_tutorial)

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(vulkan_tutorial src/main.cpp src/HelloTriangleApplication.ixx)

target_compile_features(vulkan_tutorial PUBLIC cxx_std_20)
set_target_properties(vulkan_tutorial PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(vulkan_tutorial PRIVATE glfw glm::glm Threads::Threads Vulkan::Vulkan )
target_compile_options(vulkan_tutorial PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wall -Wextra -Wpedantic -Werror>)