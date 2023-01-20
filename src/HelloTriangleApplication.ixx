module;

#define GLFW_INCLUDE_VULKAN
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_SMART_HANDLE
#define VULKAN_HPP_STORAGE_SHARED
#define VULKAN_HPP_STORAGE_SHARED_EXPORT
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <GLFW/glfw3.h>
#include <ranges>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

export module HelloTriangle;

namespace srv = std::ranges::views;
namespace vkr = vk::raii;

export constexpr auto WIDTH{800u};
export constexpr auto HEIGHT{600u};

std::vector<std::string_view> getRequiredExtensions()
{
	auto glfwExtensionCount{0u};
	auto glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};

	std::vector<std::string_view> extensionsVector{glfwExtensions, glfwExtensions + glfwExtensionCount};
	extensionsVector.emplace_back("VK_EXT_debug_utils");
	return extensionsVector;
}

export class HelloTriangleApplication
{
public:
	HelloTriangleApplication()
	{
		initWindow();
		initVulkan();
	}

	~HelloTriangleApplication() { cleanUp(); }

private:
	//	TYPES, TYPE ALIASES
	using GLFWWindowUniquePtr =
			std::unique_ptr<GLFWwindow, decltype([](auto windowPtr) { glfwDestroyWindow(windowPtr); })>;

	//	MEMBERS
	vk::DispatchLoaderDynamic loader{};
	GLFWWindowUniquePtr window;
	std::string_view applicationName{"hello triangle"};
	std::string_view engineName{"none"};
	vkr::Context context{};
	vkr::Instance instance{createInstance()};

	// INSTANCE FUNCTIONS
	void initWindow()
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		window = GLFWWindowUniquePtr(glfwCreateWindow(WIDTH, HEIGHT, "vulkan", nullptr, nullptr));
	}

	void initVulkan() {}

	void run()
	{
		while (!glfwWindowShouldClose(window.get())) { glfwPollEvents(); }
	}

	void cleanUp() { glfwTerminate(); }

	vkr::Instance createInstance()
	{
		vk::ApplicationInfo applicationInfo{.pApplicationName{applicationName.data()},
		                                    .applicationVersion{VK_MAKE_VERSION(0, 0, 1)},
		                                    .pEngineName{engineName.data()},
		                                    .engineVersion{VK_MAKE_VERSION(1, 0, 0)},
		                                    .apiVersion{context.enumerateInstanceVersion()}};

		auto glfwExtensions{getRequiredExtensions()};

		vk::InstanceCreateInfo instanceCreateInfo{
				.pApplicationInfo{&applicationInfo},
				.enabledExtensionCount{static_cast<uint32_t>(glfwExtensions.size())},
				.ppEnabledExtensionNames{reinterpret_cast<const char* const*>(glfwExtensions.data())}};

		auto availableExtensions{context.enumerateInstanceExtensionProperties()};

		if (!std::ranges::is_permutation(glfwExtensions, instanceExtensionProperties)) ;

		return context.createInstance(instanceCreateInfo);
	}
};