#pragma once

#include <GLFW/glfw3.h>
#include <ranges>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace HelloTriangle
{
namespace srv = std::ranges::views;
namespace vkr = vk::raii;

constexpr auto glfwWindowDestroyer{[](auto windowPtr)
                                          {
											  glfwDestroyWindow(windowPtr);
											  glfwTerminate();
										  }};
using GLFWWindowPointer = std::unique_ptr<GLFWwindow, decltype(glfwWindowDestroyer)>;

auto makeWindowPointer(std::uint32_t width, std::uint32_t height, std::string_view windowName) -> GLFWWindowPointer;

class Application
{
public:
	//	CONSTRUCTORS, DESTRUCTORS
	Application();

	virtual ~Application();

	auto run() -> void;

	//	INSTANCE PUBLIC

	//	STATIC PUBLIC

private:
	//	TYPES, TYPE ALIASES

	//	MEMBERS
	VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;
	uint32_t const width{800u};
	uint32_t const height{600u};
	std::string_view windowName{"Vulkan triangle"};
	std::string_view applicationName{"Hello Triangle"};
	std::uint32_t applicationVersion{VK_MAKE_API_VERSION(0, 1, 0, 0)};
	std::string_view engineName{"No engine"};
	std::uint32_t engineVersion{VK_MAKE_API_VERSION(0, 1, 0, 0)};
	GLFWWindowPointer window{HelloTriangle::makeWindowPointer(800, 600, windowName.data())};
	vkr::Context context{};
	std::array<std::string_view, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};
	vkr::Instance instance{makeInstance()};
#ifdef NDEBUG
	bool const enableValidationLayers{false};
#else
	bool const enableValidationLayers{true};
#endif

	//  INSTANCE PRIVATE
	auto mainLoop() -> void;
	auto makeInstance() -> vkr::Instance;
	auto checkValidationLayerSupport() -> bool;

	//  STATIC PRIVATE
};
}// namespace HelloTriangle