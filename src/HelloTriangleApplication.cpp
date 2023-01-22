#include "HelloTriangleApplication.hpp"

#include <algorithm>

namespace HelloTriangle
{
auto makeWindowPointer(const std::uint32_t width = 800, const std::uint32_t height = 600,
                                    std::string_view windowName = "empty") -> GLFWWindowPointer
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	return GLFWWindowPointer{glfwCreateWindow(width, height, windowName.data(), nullptr, nullptr)};
}

namespace
{
auto getGLFWInstanceExtensions() -> std::vector<std::string_view>
{
	static auto glfwExtensionCount{0u};
	static auto glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};
	static auto glfwExtensionsVec{std::vector<std::string_view>{glfwExtensions, glfwExtensions + glfwExtensionCount}};

	return glfwExtensionsVec;
}
}// namespace

Application::Application() = default;

Application::~Application() = default;

auto Application::run() -> void { mainLoop(); }

auto Application::mainLoop() -> void
{
	while (!glfwWindowShouldClose(window.get())) {
		glfwPollEvents();
	}
}

auto Application::makeInstance() -> vkr::Instance
{
	if (enableValidationLayers && !checkValidationLayerSupport())
		throw std::runtime_error("validation layers requested but not available");
	auto applicationInfo{vk::ApplicationInfo{.pApplicationName{applicationName.data()},
	                                         .applicationVersion{applicationVersion},
	                                         .pEngineName{engineName.data()},
	                                         .engineVersion{engineVersion},
	                                         .apiVersion{VK_API_VERSION_1_3}}};
	auto glfwInstanceExtensions{getGLFWInstanceExtensions()};
	auto instanceCreateInfo{vk::InstanceCreateInfo{
			.pApplicationInfo{&applicationInfo},
			.enabledExtensionCount{static_cast<uint32_t>(glfwInstanceExtensions.size())},
			.ppEnabledExtensionNames{reinterpret_cast<char const* const*>(glfwInstanceExtensions.data())}}};

	return {context, instanceCreateInfo};
}

auto Application::checkValidationLayerSupport() -> bool
{
	auto availableValidationLayers{vk::enumerateInstanceLayerProperties()};
	auto stringViewTransform{[](auto const& layer) { return std::string_view{layer.layerName}; }};

	std::ranges::sort(validationLayers);
	std::ranges::sort(availableValidationLayers, {}, stringViewTransform);
	return std::ranges::includes(availableValidationLayers, validationLayers, {}, stringViewTransform);
}

}// namespace HelloTriangle
