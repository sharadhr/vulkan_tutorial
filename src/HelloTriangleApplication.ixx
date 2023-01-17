module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

export module HelloTriangle;

import std.core;

namespace rv = std::ranges::views;

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* createInfoPtr,
                                      const VkAllocationCallbacks* allocatorPtr,
                                      VkDebugUtilsMessengerEXT* debugMessengerPtr)
{
	auto func{reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"))};
	if (func == nullptr) { return VK_ERROR_EXTENSION_NOT_PRESENT; }
	return func(instance, createInfoPtr, allocatorPtr, debugMessengerPtr);
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* allocatorPtr)
{
	auto func{reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))};
	if (func == nullptr) { return; }
	func(instance, debugMessenger, allocatorPtr);
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

	void run() { mainLoop(); }

private:
	// TYPES
	using GLFWWindowUniquePtr =
			std::unique_ptr<GLFWwindow, decltype([](auto windowPtr) { glfwDestroyWindow(windowPtr); })>;

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily{};

		[[nodiscard]] bool isComplete() const { return graphicsFamily.has_value(); }
	};

	// MEMBERS
	static constexpr auto WIDTH{800u};
	static constexpr auto HEIGHT{600u};
	GLFWWindowUniquePtr window;
	VkInstance instance{};
	std::array<const char*, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};
	VkDebugUtilsMessengerEXT debugMessenger{};
	VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
	VkDevice device{};
	VkQueue graphicsQueue{};
	VkSurfaceKHR surface{};
#ifdef NDEBUG
	const bool enableValidationLayers{false};
#else
	const bool enableValidationLayers{true};
#endif

	// INSTANCE
	void initWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = GLFWWindowUniquePtr{glfwCreateWindow(WIDTH, HEIGHT, "vulkan", nullptr, nullptr)};
	}

	void initVulkan()
	{
		createInstance();
		setupDebugMessenger();
		pickPhysicalDevice();
		createLogicalDevice();
	}

	void mainLoop()
	{
		while (!glfwWindowShouldClose(window.get())) glfwPollEvents();
	}

	void cleanUp()
	{
		vkDestroyDevice(device, nullptr);
		if (enableValidationLayers) { DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr); }


		vkDestroySurfaceKHR(instance, surface, nullptr);
		vkDestroyInstance(instance, nullptr);
		glfwTerminate();
	}

	void createInstance()
	{
		if (enableValidationLayers && !checkValidationLayersSupport())
			throw std::runtime_error("validation layers requested but not available");

		auto extensions{getRequiredExtensions()};

		VkApplicationInfo appInfo{.sType{VK_STRUCTURE_TYPE_APPLICATION_INFO},
		                          .pNext{nullptr},
		                          .pApplicationName{"Hello Triangle"},
		                          .applicationVersion{VK_MAKE_VERSION(0, 0, 1)},
		                          .pEngineName{"no engine"},
		                          .engineVersion{VK_MAKE_VERSION(0, 0, 1)},
		                          .apiVersion{VK_API_VERSION_1_0}};

		auto debugCreateInfo{populatedDebugMessengerCreateInfo()};

		VkInstanceCreateInfo createInfo{
				.sType{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO},
				.pNext{enableValidationLayers ? static_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&debugCreateInfo)
		                                      : nullptr},
				.pApplicationInfo{&appInfo},
				.enabledLayerCount{static_cast<uint32_t>(enableValidationLayers ? validationLayers.size() : 0u)},
				.ppEnabledLayerNames{enableValidationLayers ? validationLayers.data() : nullptr},
				.enabledExtensionCount{static_cast<uint32_t>(extensions.size())},
				.ppEnabledExtensionNames{extensions.data()},
		};


		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
			throw std::runtime_error("failed to create Vulkan instance");
		}
	}

	bool checkValidationLayersSupport()
	{
		auto layerCount{0u};
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers{layerCount};
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		const auto availableLayersNames =
				availableLayers | rv::transform([](const auto& availableLayer) { return availableLayer.layerName; });

		return std::ranges::all_of(validationLayers, [&](auto validationLayerName) {
			return std::ranges::find_if(availableLayersNames, [&](auto availableLayerName) {
					   return strcmp(validationLayerName, availableLayerName) == 0;
				   }) != std::end(availableLayersNames);
		});
	}

	void setupDebugMessenger()
	{
		if (!enableValidationLayers) return;
		auto createInfo{populatedDebugMessengerCreateInfo()};

		if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
			throw std::runtime_error("failed to set up debug messenger");
		}
	}

	void pickPhysicalDevice()
	{
		auto deviceCount{0u};
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount == 0) { throw std::runtime_error("no GPUs with Vulkan support"); }

		std::vector<VkPhysicalDevice> devices{deviceCount};
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		physicalDevice = *std::ranges::find_if(devices, [&](const auto& dev) { return isDeviceSuitable(dev); });

		if (physicalDevice == VK_NULL_HANDLE) { throw std::runtime_error("failed to find a suitable GPU"); }
	}

	static QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& device)
	{
		QueueFamilyIndices indices{};

		auto queueFamilyCount{0u};
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies{queueFamilyCount};
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		for (auto i{0u}; i < queueFamilies.size(); ++i) {
			if (queueFamilies.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
				break;
			}
		}

		return indices;
	}

	void createLogicalDevice()
	{
		auto indices{findQueueFamilies(physicalDevice)};
		auto queuePriorities{1.0f};

		VkDeviceQueueCreateInfo queueCreateInfo{.sType{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO},
		                                        .queueFamilyIndex{indices.graphicsFamily.value()},
		                                        .queueCount{1},
		                                        .pQueuePriorities{&queuePriorities}};

		VkPhysicalDeviceFeatures deviceFeatures{};

		VkDeviceCreateInfo createInfo{
				.sType{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO},
				.queueCreateInfoCount{1},
				.pQueueCreateInfos{&queueCreateInfo},
				.enabledLayerCount{static_cast<uint32_t>(enableValidationLayers ? validationLayers.size() : 0u)},
				.ppEnabledLayerNames{enableValidationLayers ? validationLayers.data() : nullptr},
				.enabledExtensionCount{},
				.pEnabledFeatures{&deviceFeatures},
		};

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
			throw std::runtime_error("failed to create logical device");
		}

		vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
	}

	[[nodiscard]] std::vector<const char*> getRequiredExtensions() const
	{
		auto glfwExtensionCount{0u};
		auto glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};

		std::vector<const char*> extensions{glfwExtensions, glfwExtensions + glfwExtensionCount};

		if (enableValidationLayers) { extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }

		return extensions;
	}

	// STATIC
	static VkDebugUtilsMessengerCreateInfoEXT populatedDebugMessengerCreateInfo()
	{
		return {.sType{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT},
		        .messageSeverity{VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT},
		        .messageType{VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT},
		        .pfnUserCallback{debugCallback},
		        .pUserData{nullptr}};
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL
	debugCallback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	              [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
	              const VkDebugUtilsMessengerCallbackDataEXT* callbackDataPtr, [[maybe_unused]] void* userDataPtr)
	{
		std::cerr << "validation layer: " << callbackDataPtr->pMessage << std::endl;
		return VK_FALSE;
	}

	static bool isDeviceSuitable(const VkPhysicalDevice& device)
	{
		//		VkPhysicalDeviceProperties deviceProperties;
		//		VkPhysicalDeviceFeatures deviceFeatures;
		//		vkGetPhysicalDeviceProperties(device, &deviceProperties);
		//		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		auto indices{findQueueFamilies(device)};

		return indices.isComplete();
	}
};