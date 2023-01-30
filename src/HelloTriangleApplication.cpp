#include "HelloTriangleApplication.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <functional>
#include <iostream>
#include <set>

namespace HelloTriangle
{
namespace srv = std::ranges::views;
using namespace std::string_view_literals;
using namespace fmt::literals;

auto makeWindowPointer(const std::uint32_t width, const std::uint32_t height, std::string_view windowName) -> GLFWWindowPointer
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	return GLFWWindowPointer{glfwCreateWindow(width, height, windowName.data(), nullptr, nullptr)};
}

namespace
{
auto getGLFWInstanceExtensions() -> std::vector<char const*>
{
	static auto glfwExtensionCount{0u};
	static auto const glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};
	static auto const extensions{std::vector<char const*>(glfwExtensions, glfwExtensions + glfwExtensionCount)};

	return extensions;
}

auto getRequiredExtensions(bool const enableValidationLayers) -> std::vector<char const*>
{
	auto extensions{getGLFWInstanceExtensions()};

	if (enableValidationLayers) {
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

auto checkValidationLayerSupport(vkr::Context const& context, std::span<char const*> requiredValLayers) -> bool
{
	static auto availableValLayers{context.enumerateInstanceLayerProperties()};
	auto requiredValLayersCopy{std::vector<std::string_view>{std::begin(requiredValLayers), std::end(requiredValLayers)}};

	if (availableValLayers.empty()) {
		return false;
	}

	std::ranges::sort(availableValLayers, {}, &vk::LayerProperties::layerName);
	std::ranges::sort(requiredValLayersCopy);

	return std::ranges::includes(availableValLayers, requiredValLayersCopy, {}, &vk::LayerProperties::layerName);
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugMessageFunc(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                  vk::DebugUtilsMessageTypeFlagsEXT messageType,
                                                  vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
                                                  [[maybe_unused]] void* pUserData)
{
	static auto constexpr header{R"({messageSeverity}: {messageType}:
	Message ID Name   = <{pMessageIdName}>
	Message ID Number = {messageIDNumber}
	message           = <{pMessage}>
)"sv};
	auto out{fmt::memory_buffer()};

	fmt::format_to(std::back_inserter(out),
	               header,
	               "messageSeverity"_a = vk::to_string(messageSeverity),
	               "messageType"_a = vk::to_string(messageType),
	               "pMessageIdName"_a = pCallbackData->pMessageIdName,
	               "messageIDNumber"_a = pCallbackData->messageIdNumber,
	               "pMessage"_a = pCallbackData->pMessage);

	if (pCallbackData->queueLabelCount > 0) {
		static auto constexpr queueLabelsHeader{"\tQueue Labels:\n"sv};
		static auto constexpr queueLabel{"\t\tlabelName = <{pLabelName}>\n"sv};
		auto const labels{std::span{pCallbackData->pQueueLabels, pCallbackData->queueLabelCount}};

		fmt::format_to(std::back_inserter(out), queueLabelsHeader);
		std::ranges::for_each(labels,
		                      [&](auto const& label) { fmt::format_to(std::back_inserter(out), queueLabel, "pLabelName"_a = label.pLabelName); });
	}

	if (pCallbackData->cmdBufLabelCount > 0) {
		static auto constexpr commandBufferLabelsHeader{"\tCommand Buffer Labels:\n"sv};
		static auto constexpr commandBufferLabel{"\t\tlabelName = <{pLabelName}>\n"sv};
		auto const labels{std::span{pCallbackData->pCmdBufLabels, pCallbackData->cmdBufLabelCount}};

		fmt::format_to(std::back_inserter(out), commandBufferLabelsHeader);
		std::ranges::for_each(labels,
		                      [&](auto const& label)
		                      { fmt::format_to(std::back_inserter(out), commandBufferLabel, "pLabelName"_a = label.pLabelName); });
	}

	if (pCallbackData->objectCount > 0) {
		static auto constexpr objectsHeader{"\tObjects:\n"sv};
		static auto constexpr objectI{"\t\tObject {i}\n"sv};
		static auto constexpr objectDetails{"\t\t\tObject Type   = {objectType}\n"
		                                    "\t\t\tObject Handle = {objectHandle}\n"
		                                    "\t\t\tObject Name   = <{pObjectName}>\n"sv};
		auto const objects{std::span{pCallbackData->pObjects, pCallbackData->objectCount}};

		fmt::format_to(std::back_inserter(out), objectsHeader);
		for (auto i{0u}; i < objects.size(); ++i) {
			auto const& object{objects[i]};
			fmt::format_to(std::back_inserter(out), objectI, "i"_a = i);
			fmt::format_to(std::back_inserter(out),
			               objectDetails,
			               "objectType"_a = vk::to_string(object.objectType),
			               "objectHandle"_a = object.objectHandle,
			               object.pObjectName ? "pObjectName"_a = std::string_view{object.pObjectName} : "pObjectName"_a = ""sv);
		}
	}

	fmt::print(stderr, "{}", fmt::to_string(out));
	return vk::Bool32{VK_FALSE};
}

auto makeDebugMessengerCreateInfoEXT() -> vk::DebugUtilsMessengerCreateInfoEXT
{
	static auto constexpr severityFlags{vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError};
	static auto constexpr typeFlags{vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
	                                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance};
	auto const debugCreateInfo{
	        vk::DebugUtilsMessengerCreateInfoEXT{.messageSeverity{severityFlags},
	                                             .messageType{typeFlags},
	                                             .pfnUserCallback{reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(&debugMessageFunc)}}};

	return debugCreateInfo;
}

auto checkDeviceExtensionSupport(vkr::PhysicalDevice const& physDev, std::span<char const*> requiredExtensions) -> bool
{
	static auto availableDeviceExtensions{physDev.enumerateDeviceExtensionProperties()};
	auto requiredExtensionsCopy{std::vector<std::string_view>{std::begin(requiredExtensions), std::end(requiredExtensions)}};

	if (availableDeviceExtensions.empty()) {
		return false;
	}

	std::ranges::sort(availableDeviceExtensions, {}, &vk::ExtensionProperties::extensionName);
	std::ranges::sort(requiredExtensionsCopy);

	return std::ranges::includes(availableDeviceExtensions, requiredExtensionsCopy, {}, &vk::ExtensionProperties::extensionName);
}

auto isDeviceSuitable(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface, std::span<char const*> extensions) -> bool
{
	auto const indices{Application::QueueFamilyIndices(physDev, surface)};

	if (auto const extensionsSupported{checkDeviceExtensionSupport(physDev, extensions)}; not extensionsSupported) {
		return false;
	} else {
		return indices.isComplete() and Application::SwapchainSupportDetails(physDev, surface).isAdequate();
	}
}

auto chooseSwapPresentMode(std::span<vk::PresentModeKHR const> availablePresentModes) -> vk::PresentModeKHR
{
	static auto const presentMode{std::ranges::find_if(availablePresentModes,
	                                                   [](auto const& availablePresentMode)
	                                                   { return availablePresentMode == vk::PresentModeKHR::eMailbox; })};

	if (presentMode == std::end(availablePresentModes)) {
		return vk::PresentModeKHR::eFifo;
	}

	return *presentMode;
}
}// namespace

Application::Application() = default;

Application::~Application() = default;

auto Application::run() -> void { mainLoop(); }

auto Application::mainLoop() const -> void
{
	while (!glfwWindowShouldClose(window.get())) {
		glfwPollEvents();
	}
}

auto Application::makeInstance() -> vkr::Instance
{
	if (enableValidationLayers && !checkValidationLayerSupport(context, requiredValLayers)) {
		throw std::runtime_error("validation layers requested but not available");
	}
	auto const applicationInfo{vk::ApplicationInfo{.pApplicationName{applicationName.data()},
	                                               .applicationVersion{applicationVersion},
	                                               .pEngineName{engineName.data()},
	                                               .engineVersion{engineVersion},
	                                               .apiVersion{VK_API_VERSION_1_3}}};
	auto const debugCreateInfo{makeDebugMessengerCreateInfoEXT()};
	auto const extensions{getRequiredExtensions(enableValidationLayers)};
	auto const instanceCreateInfo{
	        vk::InstanceCreateInfo{.pNext{enableValidationLayers ? &debugCreateInfo : nullptr},
	                               .pApplicationInfo{&applicationInfo},
	                               .enabledLayerCount{static_cast<uint32_t>(enableValidationLayers ? requiredValLayers.size() : 0u)},
	                               .ppEnabledLayerNames{enableValidationLayers ? requiredValLayers.data() : nullptr},
	                               .enabledExtensionCount{static_cast<uint32_t>(extensions.size())},
	                               .ppEnabledExtensionNames{extensions.data()}}};

	return {context, instanceCreateInfo};
}

auto Application::makeDebugMessenger() -> vkr::DebugUtilsMessengerEXT
{
	auto const debugCreateInfo{makeDebugMessengerCreateInfoEXT()};
	return {instance, debugCreateInfo};
}

auto Application::makeSurface() -> vkr::SurfaceKHR
{
	if (auto result{glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window.get(), nullptr, &sfc)}; result != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface");
	}

	return {instance, sfc};
}

auto Application::pickPhysicalDevice() -> vkr::PhysicalDevice
{
	static auto const physicalDevices{vkr::PhysicalDevices(instance)};

	auto const physDevice{std::ranges::find_if(
	        physicalDevices,
	        [this](auto&& physDev) { return isDeviceSuitable(std::forward<decltype(physDev)>(physDev), surface, requiredDeviceExtensions); })};
	if (physDevice == std::end(physicalDevices)) {
		throw std::runtime_error("failed to find a suitable GPU");
	}
	return *physDevice;
}

auto Application::makeDevice() -> vkr::Device
{
	auto const queuePriority{1.0f};
	auto queueCreateInfos{std::vector<vk::DeviceQueueCreateInfo>{}};
	std::set<std::uint32_t> uniqueQueueFamilies{indices.graphicsFamily.value(), indices.presentFamily.value()};

	std::ranges::transform(uniqueQueueFamilies,
	                       std::back_inserter(queueCreateInfos),
	                       [&](auto const& queueFamily) -> vk::DeviceQueueCreateInfo
	                       { return {.queueFamilyIndex{queueFamily}, .queueCount{1}, .pQueuePriorities{&queuePriority}}; });

	auto const deviceFeatures{vk::PhysicalDeviceFeatures{}};
	auto const deviceCreateInfo{
	        vk::DeviceCreateInfo{.queueCreateInfoCount{static_cast<std::uint32_t>(queueCreateInfos.size())},
	                             .pQueueCreateInfos{queueCreateInfos.data()},
	                             .enabledLayerCount{static_cast<std::uint32_t>(enableValidationLayers ? requiredValLayers.size() : 0u)},
	                             .ppEnabledLayerNames{enableValidationLayers ? requiredValLayers.data() : nullptr},
	                             .enabledExtensionCount{static_cast<std::uint32_t>(requiredDeviceExtensions.size())},
	                             .ppEnabledExtensionNames{requiredDeviceExtensions.data()},
	                             .pEnabledFeatures{&deviceFeatures}}};

	return {physicalDevice, deviceCreateInfo};
}

auto Application::chooseSwapSurfaceFormat(std::span<vk::SurfaceFormatKHR const> availableFormats) -> vk::SurfaceFormatKHR
{
	static auto const format{std::ranges::find_if(availableFormats,
	                                              [](auto const& availableFormat) {
		                                              return availableFormat.format == vk::Format::eB8G8R8A8Srgb and
		                                                     availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
	                                              })};

	if (format == std::end(availableFormats)) {
		return *std::begin(availableFormats);
	}
	return *format;
}

auto Application::chooseSwapExtent(GLFWWindowPointer const& window, vk::SurfaceCapabilitiesKHR const& capabilities) -> vk::Extent2D
{
	if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
		return capabilities.currentExtent;
	}

	static int width;
	static int height;
	glfwGetFramebufferSize(window.get(), &width, &height);

	static auto actualExtent{vk::Extent2D{.width{static_cast<std::uint32_t>(width)}, .height{static_cast<std::uint32_t>(height)}}};

	actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
	actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

	return actualExtent;
}

auto Application::makeSwapchain() -> vkr::SwapchainKHR
{
	auto surfaceFormat{chooseSwapSurfaceFormat(swapchainSupport.formats)};
	auto presentMode{chooseSwapPresentMode(swapchainSupport.presentModes)};
	auto extent{chooseSwapExtent(window, swapchainSupport.capabilities)};
	auto imageCount{std::max(swapchainSupport.capabilities.minImageCount + 1, swapchainSupport.capabilities.maxImageCount)};
	auto indicesVec{std::vector<std::uint32_t>{indices.graphicsFamily.value(), indices.presentFamily.value()}};

	auto swapchainCreateInfo{vk::SwapchainCreateInfoKHR{
	        .surface{*surface},
	        .minImageCount{imageCount},
	        .imageFormat{surfaceFormat.format},
	        .imageColorSpace{surfaceFormat.colorSpace},
	        .imageExtent{extent},
	        .imageArrayLayers{1},
	        .imageUsage{vk::ImageUsageFlagBits::eColorAttachment},
	        .imageSharingMode{indices.graphicsFamily != indices.presentFamily ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive},
	        .queueFamilyIndexCount{indices.graphicsFamily != indices.presentFamily ? static_cast<std::uint32_t>(indicesVec.size()) : 0u},
	        .pQueueFamilyIndices{indices.graphicsFamily != indices.presentFamily ? indicesVec.data() : nullptr},
	        .preTransform{swapchainSupport.capabilities.currentTransform},
	        .compositeAlpha{vk::CompositeAlphaFlagBitsKHR::eOpaque},
	        .presentMode{presentMode},
	        .clipped{vk::Bool32{VK_TRUE}},
	        .oldSwapchain{VK_NULL_HANDLE}}};

	return {logicalDevice, swapchainCreateInfo};
}

auto Application::getImageViews() -> std::vector<vkr::ImageView>
{
	auto imageViews{std::vector<vkr::ImageView>{}};
	std::ranges::transform(swapchainImages,
	                       std::back_inserter(imageViews),
	                       [&](auto const& image)
	                       {
		                       auto imageCreateInfo{vk::ImageViewCreateInfo{.image{image},
		                                                                    .viewType{vk::ImageViewType::e2D},
		                                                                    .format{swapchainImageFormat},
		                                                                    .components{.r{vk::ComponentSwizzle::eIdentity},
		                                                                                .g{vk::ComponentSwizzle::eIdentity},
		                                                                                .b{vk::ComponentSwizzle::eIdentity},
		                                                                                .a{vk::ComponentSwizzle::eIdentity}},
		                                                                    .subresourceRange{.aspectMask{vk::ImageAspectFlagBits::eColor},
		                                                                                      .baseMipLevel{0u},
		                                                                                      .levelCount{1u},
		                                                                                      .baseArrayLayer{0u},
		                                                                                      .layerCount{1u}}}};

		                       return vkr::ImageView{logicalDevice, imageCreateInfo};
	                       });

	return imageViews;
}

auto Application::QueueFamilyIndices::findGraphicsQueueFamilyIndex(vkr::PhysicalDevice const& physDev) -> std::optional<std::uint32_t>
{
	auto const queueFamilyProps{physDev.getQueueFamilyProperties()};

	if (auto const it{std::ranges::find_if(queueFamilyProps,
	                                       [](auto const& queueFamily)
	                                       { return queueFamily.queueCount > 0 and queueFamily.queueFlags & vk::QueueFlagBits::eGraphics; })};
	    it != std::end(queueFamilyProps))
	{
		return {std::distance(std::begin(queueFamilyProps), it)};
	}

	return {};
}

auto Application::QueueFamilyIndices::findPresentQueueFamilyIndex(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
        -> std::optional<std::uint32_t>
{
	auto const queueFamilyProps{physDev.getQueueFamilyProperties()};

	for (auto i{0u}; i < queueFamilyProps.size(); ++i) {
		if (queueFamilyProps[i].queueCount > 0 and physDev.getSurfaceSupportKHR(i, *surface)) {
			return {i};
		}
	}

	return {};
}

Application::QueueFamilyIndices::QueueFamilyIndices(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
    : graphicsFamily{findGraphicsQueueFamilyIndex(physDev)},
      presentFamily{findPresentQueueFamilyIndex(physDev, surface)}
{}

auto Application::QueueFamilyIndices::isComplete() const -> bool { return graphicsFamily.has_value() and presentFamily.has_value(); }

Application::SwapchainSupportDetails::SwapchainSupportDetails(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
    : capabilities{physDev.getSurfaceCapabilitiesKHR(*surface)},
      formats{physDev.getSurfaceFormatsKHR(*surface)},
      presentModes{physDev.getSurfacePresentModesKHR(*surface)}
{}

auto Application::SwapchainSupportDetails::isAdequate() const -> bool { return not(formats.empty() or presentModes.empty()); }
}// namespace HelloTriangle
