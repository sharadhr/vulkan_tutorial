module;

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

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
		std::optional<uint32_t> presentFamily{};

		[[nodiscard]] bool isComplete() const { return graphicsFamily.has_value() and presentFamily.has_value(); }
	};

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats{};
		std::vector<VkPresentModeKHR> presentModes{};
	};

	// MEMBERS
	static constexpr auto WIDTH{800u};
	static constexpr auto HEIGHT{600u};
	GLFWWindowUniquePtr window;
	VkInstance instance{};
	const std::array<const char*, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};
	const std::array<const char*, 1> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkDebugUtilsMessengerEXT debugMessenger{};
	VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
	VkDevice device{};
	VkQueue graphicsQueue{};
	VkQueue presentQueue{};
	VkSurfaceKHR surface{};
	VkSwapchainKHR swapChain{};
	std::vector<VkImage> swapChainImages{};
	VkFormat swapChainImageFormat{};
	VkExtent2D swapChainExtent{};
	std::vector<VkImageView> swapChainImageViews{};

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
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createImageViews();
	}

	void mainLoop()
	{
		while (!glfwWindowShouldClose(window.get())) glfwPollEvents();
	}

	void cleanUp()
	{
		std::ranges::for_each(swapChainImageViews,
		                      [this](const auto& imageView) { vkDestroyImageView(device, imageView, nullptr); });
		vkDestroySwapchainKHR(device, swapChain, nullptr);
		vkDestroyDevice(device, nullptr);
		if (enableValidationLayers) { DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr); }
		vkDestroySurfaceKHR(instance, surface, nullptr);
		vkDestroyInstance(instance, nullptr);
		glfwTerminate();
	}

	void createInstance()
	{
		if (enableValidationLayers and !checkValidationLayersSupport())
			throw std::runtime_error("validation layers requested but not available");

		auto extensions{getRequiredExtensions()};

		VkApplicationInfo appInfo{.sType{VK_STRUCTURE_TYPE_APPLICATION_INFO},
		                          .pNext{nullptr},
		                          .pApplicationName{"Hello Triangle"},
		                          .applicationVersion{VK_MAKE_VERSION(0, 0, 1)},
		                          .pEngineName{"no engine"},
		                          .engineVersion{VK_MAKE_VERSION(0, 0, 1)},
		                          .apiVersion{VK_API_VERSION_1_3}};

		auto debugCreateInfo{populatedDebugMessengerCreateInfo()};

		VkInstanceCreateInfo createInfo{
				.sType{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO},
				.pNext{enableValidationLayers ? static_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&debugCreateInfo)
		                                      : nullptr},
				.pApplicationInfo{&appInfo},
				.enabledLayerCount{static_cast<uint32_t>(enableValidationLayers ? validationLayers.size() : 0u)},
				.ppEnabledLayerNames{enableValidationLayers ? validationLayers.data() : nullptr},
				.enabledExtensionCount{static_cast<uint32_t>(extensions.size())},
				.ppEnabledExtensionNames{extensions.data()}};

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

		return std::ranges::all_of(
				validationLayers,
				[&](auto validationLayerName)
				{
					return std::ranges::find_if(availableLayersNames, [&](auto availableLayerName)
			                                    { return strcmp(validationLayerName, availableLayerName) == 0; }) !=
			               std::end(availableLayersNames);
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

	void createSurface()
	{
		if (glfwCreateWindowSurface(instance, window.get(), nullptr, &surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface");
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

	bool isDeviceSuitable(const VkPhysicalDevice& physDevice)
	{
		auto indices{findQueueFamilies(physDevice)};
		bool extensionsSupported{checkDeviceExtensionSupport(physDevice)};

		bool swapChainAdequate{};

		if (extensionsSupported) {
			auto swapChainSupport{querySwapChainSupport(physDevice)};
			swapChainAdequate = !swapChainSupport.formats.empty() and !swapChainSupport.presentModes.empty();
		}

		return indices.isComplete() and extensionsSupported and swapChainAdequate;
	}

	bool checkDeviceExtensionSupport(const VkPhysicalDevice& physDevice)
	{
		auto extensionCount{0u};
		vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions{extensionCount};
		vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string_view> requiredExtensions{std::begin(deviceExtensions), std::end(deviceExtensions)};

		for (const auto& extension : availableExtensions) { requiredExtensions.erase(extension.extensionName); }

		return requiredExtensions.empty();
	}

	QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& physDevice)
	{
		QueueFamilyIndices indices{};

		auto queueFamilyCount{0u};
		vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies{queueFamilyCount};
		vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, queueFamilies.data());

		for (auto i{0u}; i < queueFamilies.size(); ++i) {
			if (queueFamilies.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) { indices.graphicsFamily = i; }

			VkBool32 presentSupport{};
			vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, i, surface, &presentSupport);

			if (presentSupport) { indices.presentFamily = i; }

			if (indices.isComplete()) { break; }
		}

		return indices;
	}

	SwapChainSupportDetails querySwapChainSupport(const VkPhysicalDevice& physDevice)
	{
		SwapChainSupportDetails details{};

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, surface, &(details.capabilities));

		auto formatCount{0u};
		vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, nullptr);
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, details.formats.data());

		auto presentModeCount{0u};
		vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &presentModeCount, nullptr);
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &presentModeCount, details.presentModes.data());

		return details;
	}

	void createLogicalDevice()
	{
		auto indices{findQueueFamilies(physicalDevice)};
		auto queuePriority{1.0f};

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
		std::set<uint32_t> uniqueQueueFamilies{indices.graphicsFamily.value(), indices.presentFamily.value()};

		std::ranges::copy(uniqueQueueFamilies | rv::transform(
														[&queuePriority](const auto& queueFamily)
														{
															return VkDeviceQueueCreateInfo{
																	.sType{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO},
																	.queueFamilyIndex{queueFamily},
																	.queueCount{1},
																	.pQueuePriorities{&queuePriority}};
														}),
		                  std::back_inserter(queueCreateInfos));

		VkPhysicalDeviceFeatures deviceFeatures{};

		VkDeviceCreateInfo createInfo{
				.sType{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO},
				.queueCreateInfoCount{static_cast<uint32_t>(queueCreateInfos.size())},
				.pQueueCreateInfos{queueCreateInfos.data()},
				.enabledLayerCount{static_cast<uint32_t>(enableValidationLayers ? validationLayers.size() : 0u)},
				.ppEnabledLayerNames{enableValidationLayers ? validationLayers.data() : nullptr},
				.enabledExtensionCount{static_cast<uint32_t>(deviceExtensions.size())},
				.ppEnabledExtensionNames{deviceExtensions.data()},
				.pEnabledFeatures{&deviceFeatures}};

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
			throw std::runtime_error("failed to create logical device");
		}

		vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
		vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
	}

	[[nodiscard]] std::vector<const char*> getRequiredExtensions() const
	{
		auto glfwExtensionCount{0u};
		auto glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};

		std::vector<const char*> extensions{glfwExtensions, glfwExtensions + glfwExtensionCount};

		if (enableValidationLayers) { extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }

		return extensions;
	}

	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		} else {
			int width;
			int height;
			glfwGetFramebufferSize(window.get(), &width, &height);

			return {.width{std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width,
			                          capabilities.maxImageExtent.width)},
			        .height{std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
			                           capabilities.maxImageExtent.height)}};
		}
	}

	void createSwapChain()
	{
		auto swapChainSupport{querySwapChainSupport(physicalDevice)};
		auto surfaceFormat{chooseSwapSurfaceFormat(swapChainSupport.formats)};
		auto presentMode{chooseSwapPresentMode(swapChainSupport.presentModes)};
		auto extent{chooseSwapExtent(swapChainSupport.capabilities)};
		auto indices{findQueueFamilies(physicalDevice)};
		std::array<uint32_t, 2> queueFamilyIndices{indices.graphicsFamily.value(), indices.presentFamily.value()};
		auto familiesSame{indices.graphicsFamily == indices.presentFamily};

		VkSwapchainCreateInfoKHR createInfo{
				.sType{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR},
				.surface{surface},
				.minImageCount{std::max(0u, swapChainSupport.capabilities.maxImageCount)},
				.imageFormat{surfaceFormat.format},
				.imageColorSpace{surfaceFormat.colorSpace},
				.imageExtent{extent},
				.imageArrayLayers{1},
				.imageUsage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT},
				.imageSharingMode{familiesSame ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT},
				.queueFamilyIndexCount{familiesSame ? 0u : 2u},
				.pQueueFamilyIndices{familiesSame ? nullptr : queueFamilyIndices.data()},
				.preTransform{swapChainSupport.capabilities.currentTransform},
				.compositeAlpha{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR},
				.presentMode{presentMode},
				.clipped{VK_TRUE},
				.oldSwapchain{VK_NULL_HANDLE}};

		if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
			throw std::runtime_error("failed to create swap chain");
		}

		vkGetSwapchainImagesKHR(device, swapChain, &(createInfo.minImageCount), nullptr);
		swapChainImages.resize(createInfo.minImageCount);
		vkGetSwapchainImagesKHR(device, swapChain, &(createInfo.minImageCount), swapChainImages.data());

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;
	}

	void createImageViews()
	{
		auto createImageView{
				[this](const VkImage& swapChainImage)
				{
					VkImageView swapChainImageView{};
					VkImageViewCreateInfo createInfo{.sType{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO},
			                                         .image{swapChainImage},
			                                         .viewType{VK_IMAGE_VIEW_TYPE_2D},
			                                         .format{swapChainImageFormat},
			                                         .components{.r{VK_COMPONENT_SWIZZLE_IDENTITY},
			                                                     .g{VK_COMPONENT_SWIZZLE_IDENTITY},
			                                                     .b{VK_COMPONENT_SWIZZLE_IDENTITY},
			                                                     .a{VK_COMPONENT_SWIZZLE_IDENTITY}},
			                                         .subresourceRange{.aspectMask{VK_IMAGE_ASPECT_COLOR_BIT},
			                                                           .baseMipLevel{0u},
			                                                           .levelCount{1u},
			                                                           .baseArrayLayer{0u},
			                                                           .layerCount{1u}}};

					if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageView) != VK_SUCCESS) {
						throw std::runtime_error("failed to create image views");
					}
					return swapChainImageView;
				}};

		std::ranges::transform(swapChainImages, std::back_inserter(swapChainImageViews), createImageView);
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

	static VkSurfaceFormatKHR chooseSwapSurfaceFormat(std::span<VkSurfaceFormatKHR const> availableFormats)
	{
		if (auto bgra_888_srgb_nonlinear{std::ranges::find_if(availableFormats,
		                                                      [](const auto& availableFormat)
		                                                      {
																  return availableFormat.format ==
			                                                                     VK_FORMAT_B8G8R8A8_SRGB and
			                                                             availableFormat.colorSpace ==
			                                                                     VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
															  })};
		    bgra_888_srgb_nonlinear != std::end(availableFormats)) {
			return *bgra_888_srgb_nonlinear;
		}
		return *std::begin(availableFormats);
	}

	static VkPresentModeKHR chooseSwapPresentMode(std::span<VkPresentModeKHR const> availablePresentModes)
	{
		if (auto mailboxPresentMode{
					std::ranges::find_if(availablePresentModes, [](const auto& availablePresentMode)
		                                 { return availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR; })};
		    mailboxPresentMode != std::end(availablePresentModes)) {
			return *mailboxPresentMode;
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}
};
