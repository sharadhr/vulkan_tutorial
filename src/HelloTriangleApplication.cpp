#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define STB_IMAGE_IMPLEMENTATION

#include "HelloTriangleApplication.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <numeric>
#include <set>
#include <stb_image.h>
#include <utility>
#include <vulkan/vulkan_raii.hpp>

namespace HelloTriangle
{
namespace fs = std::filesystem;
namespace rv = std::ranges::views;
using namespace std::string_view_literals;
using namespace fmt::literals;

namespace
{
auto getGLFWInstanceExtensions() -> std::vector<char const*>
{
	static auto       glfwExtensionCount = 0u;
	static auto const glfwExtensions     = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	static auto const extensions         = std::vector(glfwExtensions, glfwExtensions + glfwExtensionCount);

	return extensions;
}

auto getRequiredExtensions(bool const enableValidationLayers) -> std::vector<char const*>
{
	auto extensions = getGLFWInstanceExtensions();

	if (enableValidationLayers) {
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

auto checkValidationLayerSupport(vkr::Context const& context, std::span<char const*> requiredValLayers) -> bool
{
	static auto availableValLayers    = context.enumerateInstanceLayerProperties();
	auto        requiredValLayersCopy = std::vector<std::string_view>{std::begin(requiredValLayers), std::end(requiredValLayers)};

	if (availableValLayers.empty()) {
		return false;
	}

	std::ranges::sort(availableValLayers, {}, &vk::LayerProperties::layerName);
	std::ranges::sort(requiredValLayersCopy);

	return std::ranges::includes(availableValLayers, requiredValLayersCopy, {}, &vk::LayerProperties::layerName);
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugMessageFunc(vk::DebugUtilsMessageSeverityFlagBitsEXT const messageSeverity,
                                                  vk::DebugUtilsMessageTypeFlagsEXT const        messageType,
                                                  vk::DebugUtilsMessengerCallbackDataEXT const*  pCallbackData,
                                                  [[maybe_unused]] void*                         pUserData)
{
	constexpr static auto header = R"({severity} --- {type}:
	Message ID Name   = <{id}>
	Message ID Number = {number}
	message           = <{message}>
)"sv;
	auto                  out    = fmt::memory_buffer{};

	fmt::format_to(std::back_inserter(out),
	               header,
	               "severity"_a = to_string(messageSeverity),
	               "type"_a     = to_string(messageType),
	               "id"_a       = pCallbackData->pMessageIdName,
	               "number"_a   = pCallbackData->messageIdNumber,
	               "message"_a  = pCallbackData->pMessage);

	if (pCallbackData->queueLabelCount > 0) {
		constexpr static auto queueLabelsHeader = "\tQueue Labels:\n"sv;
		constexpr static auto queueLabel        = "\t\tlabelName = <{labelName}>\n"sv;
		auto const            labels            = std::span{pCallbackData->pQueueLabels, pCallbackData->queueLabelCount};

		fmt::format_to(std::back_inserter(out), queueLabelsHeader);
		std::ranges::for_each(labels,
		                      [&](auto const& label) { fmt::format_to(std::back_inserter(out), queueLabel, "labelName"_a = label.pLabelName); });
	}

	if (pCallbackData->cmdBufLabelCount > 0) {
		constexpr static auto commandBufferLabelsHeader{"\tCommand Buffer Labels:\n"sv};
		constexpr static auto commandBufferLabel{"\t\tlabelName = <{labelName}>\n"sv};
		auto const            labels = std::span{pCallbackData->pCmdBufLabels, pCallbackData->cmdBufLabelCount};

		fmt::format_to(std::back_inserter(out), commandBufferLabelsHeader);
		std::ranges::for_each(labels,
		                      [&](auto const& label)
		                      { fmt::format_to(std::back_inserter(out), commandBufferLabel, "labelName"_a = label.pLabelName); });
	}

	if (pCallbackData->objectCount > 0) {
		constexpr static auto objectsHeader = "\tObjects:\n"sv;
		constexpr static auto objectI       = "\t\tObject {i}\n"sv;
		constexpr static auto objectDetails = "\t\t\tObject Type   = {objectType}\n"
		                                      "\t\t\tObject Handle = {objectHandle}\n"
		                                      "\t\t\tObject Name   = <{pObjectName}>\n"sv;
		auto const            objects       = std::span{pCallbackData->pObjects, pCallbackData->objectCount};

		fmt::format_to(std::back_inserter(out), objectsHeader);
		for (auto i{0u}; i < objects.size(); ++i) {
			auto const& object = objects[i];
			fmt::format_to(std::back_inserter(out), objectI, "i"_a = i);
			fmt::format_to(std::back_inserter(out),
			               objectDetails,
			               "objectType"_a   = to_string(object.objectType),
			               "objectHandle"_a = object.objectHandle,
			               object.pObjectName ? "pObjectName"_a = std::string_view{object.pObjectName} : "pObjectName"_a = ""sv);
		}
	}

	fmt::print(stderr, "{}", fmt::to_string(out));

	return VK_FALSE;
}

auto makeDebugMessengerCreateInfoEXT() -> vk::DebugUtilsMessengerCreateInfoEXT
{
	constexpr static auto severityFlags = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
	constexpr static auto typeFlags     = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
	                                  vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
	auto const debugCreateInfo =
	    vk::DebugUtilsMessengerCreateInfoEXT{{}, severityFlags, typeFlags, reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(&debugMessageFunc)};

	return debugCreateInfo;
}

auto checkDeviceExtensionSupport(vkr::PhysicalDevice const& physDev, std::span<char const*> requiredExtensions) -> bool
{
	static auto availableDeviceExtensions = physDev.enumerateDeviceExtensionProperties();
	auto        requiredExtensionsCopy    = std::vector<std::string_view>{std::begin(requiredExtensions), std::end(requiredExtensions)};

	if (availableDeviceExtensions.empty()) {
		return false;
	}

	std::ranges::sort(availableDeviceExtensions, {}, &vk::ExtensionProperties::extensionName);
	std::ranges::sort(requiredExtensionsCopy);

	return std::ranges::includes(availableDeviceExtensions, requiredExtensionsCopy, {}, &vk::ExtensionProperties::extensionName);
}

auto isDeviceSuitable(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface, std::span<char const*> const extensions) -> bool
{
	auto const indices           = Application::QueueFamilyIndices(physDev, surface);
	auto const supportedFeatures = physDev.getFeatures();

	if (auto const extensionsSupported = checkDeviceExtensionSupport(physDev, extensions); not extensionsSupported) {
		return false;
	}

	return indices.isComplete() and Application::SwapchainSupportDetails(physDev, surface).isAdequate() and supportedFeatures.samplerAnisotropy;
}

auto chooseSwapPresentMode(std::span<vk::PresentModeKHR const> availablePresentModes) -> vk::PresentModeKHR
{
	static auto const presentMode =
	    std::ranges::find_if(availablePresentModes,
	                         [](auto const& availablePresentMode) { return availablePresentMode == vk::PresentModeKHR::eMailbox; });

	if (presentMode == std::end(availablePresentModes)) {
		return vk::PresentModeKHR::eFifo;
	}

	return *presentMode;
}

auto frameBufferResizeCallback = [](GLFWwindow* window, [[maybe_unused]] int width, [[maybe_unused]] int height)
{
	auto const app          = static_cast<Application*>(glfwGetWindowUserPointer(window));
	app->framebufferResized = true;
};

auto hasStencilComponent(vk::Format const& format) -> bool { return format == vk::Format::eD32SfloatS8Uint or format == vk::Format::eD24UnormS8Uint; }
}// namespace

auto makeWindowPointer(Application& app, std::uint32_t const width, std::uint32_t const height, std::string_view const windowName)
    -> GLFWWindowPointer
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto windowPtr = GLFWWindowPointer{glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), windowName.data(), nullptr, nullptr)};

	glfwSetWindowUserPointer(windowPtr.get(), &app);
	glfwSetFramebufferSizeCallback(windowPtr.get(), frameBufferResizeCallback);

	return windowPtr;
}

Application::Application() = default;

Application::~Application() = default;

auto Application::run() -> void { mainLoop(); }

auto Application::mainLoop() -> void
{
	while (!glfwWindowShouldClose(window.get())) {
		glfwPollEvents();
		try {
			drawFrame();
		} catch (vk::OutOfDateKHRError const&) {
			framebufferResized = false;
			remakeSwapchain();
		}
	}
	logicalDevice.waitIdle();
}

auto Application::makeInstance() const -> vkr::Instance
{
	if (enableValidationLayers && !checkValidationLayerSupport(context, validationLayers)) {
		throw std::runtime_error("validation layers requested but not available");
	}
	auto const applicationInfo =
	    vk::ApplicationInfo{applicationName.data(), applicationVersion, engineName.data(), engineVersion, VK_API_VERSION_1_3};
	auto const debugCreateInfo = makeDebugMessengerCreateInfoEXT();
	auto const extensions      = getRequiredExtensions(enableValidationLayers);

	if (enableValidationLayers) {
		auto const instanceCreateInfo = vk::InstanceCreateInfo{{}, &applicationInfo, validationLayers, extensions, &debugCreateInfo};
		return context.createInstance(instanceCreateInfo);
	}

	auto const instanceCreateInfo = vk::InstanceCreateInfo{{}, &applicationInfo, {}, extensions};

	return context.createInstance(instanceCreateInfo);
}

auto Application::makeDebugMessenger() const -> vkr::DebugUtilsMessengerEXT
{
	auto const debugCreateInfo = makeDebugMessengerCreateInfoEXT();
	return instance.createDebugUtilsMessengerEXT(debugCreateInfo);
}

auto Application::makeSurface() -> vkr::SurfaceKHR
{
	if (auto const result = glfwCreateWindowSurface(*instance, window.get(), nullptr, &sfc); result != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface");
	}

	return {instance, sfc};
}

auto Application::pickPhysicalDevice() -> vkr::PhysicalDevice
{
	static auto const physicalDevices = vkr::PhysicalDevices(instance);
	auto const        physDevice =
	    std::ranges::find_if(physicalDevices, [this](auto const& physDev) { return isDeviceSuitable(physDev, surface, requiredDeviceExtensions); });

	if (physDevice == std::end(physicalDevices)) {
		throw std::runtime_error("failed to find a suitable GPU");
	}

	return *physDevice;
}

auto Application::makeDevice() const -> vkr::Device
{
	constexpr auto queuePriorities     = std::array{1.0f};
	auto           queueCreateInfos    = std::vector<vk::DeviceQueueCreateInfo>{};
	auto           uniqueQueueFamilies = std::set{queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()};

	std::ranges::transform(uniqueQueueFamilies,
	                       std::back_inserter(queueCreateInfos),
	                       [&](auto const& queueFamily) -> vk::DeviceQueueCreateInfo {
		                       return {{}, queueFamily, queuePriorities};
	                       });

	auto deviceFeatures              = vk::PhysicalDeviceFeatures{};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	if (enableValidationLayers) {
		auto const deviceCreateInfo{vk::DeviceCreateInfo{{}, queueCreateInfos, validationLayers, requiredDeviceExtensions, &deviceFeatures}};
		return physicalDevice.createDevice(deviceCreateInfo);
	}

	auto const deviceCreateInfo = vk::DeviceCreateInfo{{}, queueCreateInfos, {}, requiredDeviceExtensions, &deviceFeatures};

	return physicalDevice.createDevice(deviceCreateInfo);
}

auto Application::chooseSwapSurfaceFormat(std::span<vk::SurfaceFormatKHR const> availableFormats) -> vk::SurfaceFormatKHR
{
	static auto const format = std::ranges::find_if(availableFormats,
	                                                [](auto const& availableFormat) {
		                                                return availableFormat.format == vk::Format::eB8G8R8A8Srgb and
		                                                       availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
	                                                });

	if (format == std::end(availableFormats)) {
		return availableFormats.front();
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

	static auto actualExtent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};

	actualExtent.width  = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
	actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

	return actualExtent;
}

auto Application::makeSwapchain() -> vkr::SwapchainKHR
{
	auto const newSwapchainSupport = SwapchainSupportDetails{physicalDevice, surface};
	swapchainSupport               = newSwapchainSupport;

	auto const [format, colourSpace] = chooseSwapSurfaceFormat(swapchainSupport.formats);
	auto const presentMode           = chooseSwapPresentMode(swapchainSupport.presentModes);
	auto const extent                = chooseSwapExtent(window, swapchainSupport.capabilities);
	auto const imageCount            = std::max(swapchainSupport.capabilities.minImageCount + 1, swapchainSupport.capabilities.maxImageCount);
	auto const indices               = queueFamilyIndices.indices();
	auto const swapchainCreateInfo   = vk::SwapchainCreateInfoKHR{
	      {},
        *surface,
        imageCount,
        format,
        colourSpace,
        extent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        indices,
        swapchainSupport.capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        presentMode,
        VK_TRUE};

	if (format != swapchainImageFormat or extent != swapchainExtent) {
		swapchainImageFormat = format;
		swapchainExtent      = extent;
	}

	return logicalDevice.createSwapchainKHR(swapchainCreateInfo);
}

auto Application::makeImageViews() -> std::vector<vkr::ImageView>
{
	auto imageViews = std::vector<vkr::ImageView>{};
	imageViews.reserve(swapchain.getImages().size());
	std::ranges::transform(swapchain.getImages(),
	                       std::back_inserter(imageViews),
	                       [this](auto const& image) -> vkr::ImageView
	                       { return makeImageView(image, swapchainImageFormat, vk::ImageAspectFlagBits::eColor); });

	return imageViews;
}

auto Application::makeImageView(vk::Image const& image, vk::Format const& format, vk::ImageAspectFlags const& aspectFlags) const -> vkr::ImageView
{
	auto const imageCreateInfo = vk::ImageViewCreateInfo{{}, image, vk::ImageViewType::e2D, format, {}, {aspectFlags, 0, 1, 0, 1}};

	return logicalDevice.createImageView(imageCreateInfo);
}

auto Application::readFile(fs::path const& filePath) -> std::vector<std::byte>
{
	auto file = std::ifstream{filePath, std::ios::in | std::ios::binary};
	if (!file.is_open()) {
		throw std::runtime_error{std::format("failed to open file: {}", filePath.string())};
	}

	auto const fileSize = file_size(filePath);
	auto       buffer   = std::vector<std::byte>(fileSize);
	file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));

	return buffer;
}

auto Application::makeShaderModule(std::span<std::byte const> const shaderCode) const -> vkr::ShaderModule
{
	auto const shaderModuleCreateInfo = vk::ShaderModuleCreateInfo{{}, shaderCode.size(), reinterpret_cast<uint32_t const*>(shaderCode.data())};

	return logicalDevice.createShaderModule(shaderModuleCreateInfo);
}

auto Application::makeRenderPass() const -> vkr::RenderPass
{
	auto const     colourAttachment    = vk::AttachmentDescription{{},
                                                            swapchainImageFormat,
                                                            vk::SampleCountFlagBits::e1,
                                                            vk::AttachmentLoadOp::eClear,
                                                            vk::AttachmentStoreOp::eStore,
                                                            vk::AttachmentLoadOp::eDontCare,
                                                            vk::AttachmentStoreOp::eDontCare,
                                                            vk::ImageLayout::eUndefined,
                                                            vk::ImageLayout::ePresentSrcKHR};
	constexpr auto colourAttachmentRef = vk::AttachmentReference{{0}, vk::ImageLayout::eColorAttachmentOptimal};

	auto const     depthAttachment    = vk::AttachmentDescription{{},
                                                           findDepthFormat(),
                                                           vk::SampleCountFlagBits::e1,
                                                           vk::AttachmentLoadOp::eClear,
                                                           vk::AttachmentStoreOp::eDontCare,
                                                           vk::AttachmentLoadOp::eDontCare,
                                                           vk::AttachmentStoreOp::eDontCare,
                                                           vk::ImageLayout::eUndefined,
                                                           vk::ImageLayout::eDepthStencilAttachmentOptimal};
	constexpr auto depthAttachmentRef = vk::AttachmentReference{1u, vk::ImageLayout::eDepthStencilAttachmentOptimal};

	auto const     subpass = vk::SubpassDescription{{}, vk::PipelineBindPoint::eGraphics, {}, colourAttachmentRef, {}, &depthAttachmentRef};
	constexpr auto dependency =
	    vk::SubpassDependency{VK_SUBPASS_EXTERNAL,
	                          {},
	                          vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
	                          vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
	                          {},
	                          vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite};

	auto const attachments    = std::array{colourAttachment, depthAttachment};
	auto const renderPassInfo = vk::RenderPassCreateInfo{{}, attachments, subpass, dependency};

	return logicalDevice.createRenderPass(renderPassInfo);
}

auto Application::makeDescriptorSetLayout() const -> vkr::DescriptorSetLayout
{
	constexpr auto mvprojLayoutBinding = vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex};
	constexpr auto samplerLayoutBinding =
	    vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eCombinedImageSampler, 1u, vk::ShaderStageFlagBits::eFragment};
	constexpr auto layoutBindings = std::array{mvprojLayoutBinding, samplerLayoutBinding};
	auto const     layoutInfo     = vk::DescriptorSetLayoutCreateInfo{{}, layoutBindings};

	return logicalDevice.createDescriptorSetLayout(layoutInfo);
}

auto Application::makeGraphicsPipeline() const -> PipelineLayoutAndPipeline
{
	auto const     vertShaderCode = readFile("triangle.vert.spv"sv);
	auto const     fragShaderCode = readFile("triangle.frag.spv"sv);

	auto const     vertShaderModule = makeShaderModule(vertShaderCode);
	auto const     fragShaderModule = makeShaderModule(fragShaderCode);

	auto const     vertShaderStageInfo = vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertShaderModule, "main"};
	auto const     fragShaderStageInfo = vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragShaderModule, "main"};
	auto const     shaderStages        = std::array{vertShaderStageInfo, fragShaderStageInfo};

	constexpr auto dynamicStates = std::array{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	auto const     dynamicState  = vk::PipelineDynamicStateCreateInfo{{}, dynamicStates};

	constexpr auto bindingDescription   = Vertex::getBindingDescription();
	constexpr auto attributeDescription = Vertex::getAttributeDescriptions();
	auto const     vertexInputInfo      = vk::PipelineVertexInputStateCreateInfo{{}, bindingDescription, attributeDescription};

	constexpr auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{{}, vk::PrimitiveTopology::eTriangleList};
	auto const viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f};

	auto const scissor       = vk::Rect2D{{0, 0}, swapchainExtent};
	auto const viewportState = vk::PipelineViewportStateCreateInfo{{}, 1u, &viewport, 1u, &scissor};
	constexpr auto depthStencil =
	    vk::PipelineDepthStencilStateCreateInfo{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE, VK_FALSE, {}, {}, {}, 1.0f};
	constexpr auto rasteriser            = vk::PipelineRasterizationStateCreateInfo{{},
                                                                         VK_FALSE,
                                                                         VK_FALSE,
                                                                         vk::PolygonMode::eFill,
                                                                         vk::CullModeFlagBits::eBack,
                                                                         vk::FrontFace::eCounterClockwise,
                                                                         VK_FALSE,
	                                                                                {},
	                                                                                {},
	                                                                                {},
                                                                         1.0f};
	constexpr auto multisampling         = vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1};
	constexpr auto colourBlendAttachment = vk::PipelineColorBlendAttachmentState{{},
	                                                                             vk::BlendFactor::eOne,
	                                                                             vk::BlendFactor::eOne,
	                                                                             {},
	                                                                             {},
	                                                                             {},
	                                                                             {},
	                                                                             vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eB |
	                                                                                 vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eA};
	auto const     colourBlending        = vk::PipelineColorBlendStateCreateInfo{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &colourBlendAttachment};
	auto const     pipelineLayoutInfo    = vk::PipelineLayoutCreateInfo{{}, *descriptorSetLayout};
	auto           retPipelineLayout     = logicalDevice.createPipelineLayout(pipelineLayoutInfo);
	auto const     pipelineInfo          = vk::GraphicsPipelineCreateInfo{{},
                                                             shaderStages,
                                                             &vertexInputInfo,
                                                             &inputAssembly,
	                                                                      {},
                                                             &viewportState,
                                                             &rasteriser,
                                                             &multisampling,
                                                             &depthStencil,
                                                             &colourBlending,
                                                             &dynamicState,
                                                             *retPipelineLayout,
                                                             *renderPass,
                                                             0u,
	                                                                      {},
                                                             -1};

	auto           retGraphicsPipeline = logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo);

	return {std::move(retPipelineLayout), std::move(retGraphicsPipeline)};
}

auto Application::makeFramebuffers() -> std::vector<vkr::Framebuffer>
{
	auto framebuffers = std::vector<vkr::Framebuffer>{};
	framebuffers.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::transform(
	    swapchainImageViews,
	    std::back_inserter(framebuffers),
	    [&](auto const& attachments)
	    {
		    auto const framebufferInfo = vk::FramebufferCreateInfo{{}, *renderPass, attachments, swapchainExtent.width, swapchainExtent.height, 1u};

		    return logicalDevice.createFramebuffer(framebufferInfo);
	    },
	    [this](auto const& imageView) {
		    return std::array{*imageView, *depthImageView};
	    });

	return framebuffers;
}

auto Application::makeCommandPool() const -> vkr::CommandPool
{
	auto const poolInfo = vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndices.graphicsFamily.value()};

	return logicalDevice.createCommandPool(poolInfo);
}

auto Application::makeCommandBuffers() const -> vkr::CommandBuffers
{
	auto const allocInfo = vk::CommandBufferAllocateInfo{*commandPool, vk::CommandBufferLevel::ePrimary, MAX_FRAMES_IN_FLIGHT};

	return {logicalDevice, allocInfo};
}

auto Application::recordCommandBuffer(vkr::CommandBuffer const& commandBuffer, std::uint32_t const imageIndex) -> void
{
	constexpr auto beginInfo = vk::CommandBufferBeginInfo{};
	commandBuffer.begin(beginInfo);

	constexpr auto clearColours =
	    std::array{vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}}, vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0u}}};
	auto const renderPassInfo = vk::RenderPassBeginInfo{*renderPass, *swapchainFramebuffers.at(imageIndex), {{}, swapchainExtent}, clearColours};

	commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *layoutAndPipeline.second);

	constexpr auto offset = vk::DeviceSize{0};

	commandBuffer.bindVertexBuffers(0u, *vertexBufferAndMemory.first, offset);
	commandBuffer.bindIndexBuffer(*indexBufferAndMemory.first, 0, vk::IndexType::eUint16);

	auto const viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f};
	commandBuffer.setViewport(0, viewport);

	auto const scissor = vk::Rect2D{{}, swapchainExtent};
	commandBuffer.setScissor(0, scissor);

	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layoutAndPipeline.first, {}, *descriptorSets[currentFrameIndex], {});

	commandBuffer.drawIndexed(vertexIndices.size(), 1, 0, 0, 0);
	commandBuffer.endRenderPass();
	commandBuffer.end();
}

auto Application::drawFrame() -> void
{
	if (auto const waitResult =
	        logicalDevice.waitForFences(*inFlightFences.at(currentFrameIndex), VK_TRUE, std::numeric_limits<std::uint64_t>::max());
	    waitResult != vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to wait for fences");
	}

	auto const [acquireResult, imageIndex] =
	    swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *imageAvailableSemaphores.at(currentFrameIndex));
	if (acquireResult != vk::Result::eSuccess and acquireResult != vk::Result::eSuboptimalKHR) {
		throw std::runtime_error{"Failed to acquire swapchain image"};
	}

	logicalDevice.resetFences(*inFlightFences.at(currentFrameIndex));

	commandBuffers.at(currentFrameIndex).reset();
	recordCommandBuffer(commandBuffers.at(currentFrameIndex), imageIndex);

	auto const&    waitSemaphores       = *imageAvailableSemaphores.at(currentFrameIndex);
	auto const&    signalSemaphores     = *renderFinishedSemaphores.at(currentFrameIndex);
	auto const&    submitCommandBuffers = *commandBuffers.at(currentFrameIndex);
	constexpr auto waitStages           = vk::Flags{vk::PipelineStageFlagBits::eColorAttachmentOutput};

	updateUniformBuffer(currentFrameIndex);

	auto const submitInfo = vk::SubmitInfo{waitSemaphores, waitStages, submitCommandBuffers, signalSemaphores};
	graphicsQueue.submit(submitInfo, *inFlightFences.at(currentFrameIndex));

	if (auto const presentResult = presentQueue.presentKHR(vk::PresentInfoKHR{signalSemaphores, *swapchain, imageIndex});
	    presentResult == vk::Result::eSuboptimalKHR or framebufferResized)
	{
		framebufferResized = false;
		remakeSwapchain();
	} else if (presentResult != vk::Result::eSuccess) {
		throw std::runtime_error("failed to present swapchain image");
	}

	++currentFrameIndex;
	currentFrameIndex %= MAX_FRAMES_IN_FLIGHT;
}

auto Application::makeSemaphores() const -> std::vector<vkr::Semaphore>
{
	constexpr auto semaphoreInfo = vk::SemaphoreCreateInfo{};
	auto           semaphores    = std::vector<vkr::Semaphore>{};
	semaphores.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::generate_n(std::back_inserter(semaphores), MAX_FRAMES_IN_FLIGHT, [&] { return vkr::Semaphore{logicalDevice, semaphoreInfo}; });

	return semaphores;
}

auto Application::makeFences() const -> std::vector<vkr::Fence>
{
	constexpr auto fenceInfo = vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled};
	auto           fences    = std::vector<vkr::Fence>{};
	fences.reserve(MAX_FRAMES_IN_FLIGHT);
	std::ranges::generate_n(std::back_inserter(fences), MAX_FRAMES_IN_FLIGHT, [&] { return logicalDevice.createFence(fenceInfo); });

	return fences;
}

auto Application::remakeSwapchain() -> void
{
	auto newWidth{0};
	auto newHeight{0};
	glfwGetFramebufferSize(window.get(), &newWidth, &newHeight);

	while (newWidth == 0 or newHeight == 0) {
		glfwGetFramebufferSize(window.get(), &newWidth, &newHeight);
		glfwWaitEvents();
	}

	logicalDevice.waitIdle();

	swapchainFramebuffers.clear();
	swapchainImageViews.clear();
	swapchain.clear();

	swapchain             = makeSwapchain();
	swapchainImageViews   = makeImageViews();
	swapchainFramebuffers = makeFramebuffers();
}

auto Application::findMemoryType(std::uint32_t const typeFilter, vk::MemoryPropertyFlags const& flags) const -> std::uint32_t
{
	for (auto const memProperties{physicalDevice.getMemoryProperties()}; auto const i : rv::iota(0u, memProperties.memoryTypeCount)) {
		if (typeFilter & (1 << i) and (memProperties.memoryTypes.at(i).propertyFlags & flags) == flags) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type");
}

auto Application::makeBufferAndMemory(vk::DeviceSize const size, vk::BufferUsageFlags const& usage, vk::MemoryPropertyFlags const& properties) const
    -> BufferAndMemory
{
	auto const bufferInfo = vk::BufferCreateInfo{{}, size, usage};
	auto       retBuffer  = logicalDevice.createBuffer(bufferInfo);

	auto const memoryRequirements = retBuffer.getMemoryRequirements();
	auto const allocInfo          = vk::MemoryAllocateInfo{memoryRequirements.size, findMemoryType(memoryRequirements.memoryTypeBits, properties)};
	auto       retBufferMemory    = logicalDevice.allocateMemory(allocInfo);
	retBuffer.bindMemory(*retBufferMemory, 0);

	return std::make_pair(std::move(retBuffer), std::move(retBufferMemory));
}

auto Application::copyBuffer(vkr::Buffer const& srcBuffer, vkr::Buffer const& dstBuffer, vk::DeviceSize const size) const -> void
{
	auto       commandBuffer = beginSingleTimeCommands();
	auto const copyRegion    = vk::BufferCopy{{}, {}, size};

	commandBuffer.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);

	endSingleTimeCommands(std::move(commandBuffer));
}

auto Application::makeVertexBuffer() const -> BufferAndMemory
{
	using vertexType = decltype(vertices)::value_type;

	auto const bufferSize = sizeof(vertexType) * vertices.size();
	auto const [stagingBuffer, stagingBufferMemory] =
	    makeBufferAndMemory(bufferSize,
	                        vk::BufferUsageFlagBits::eTransferSrc,
	                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	auto const data = static_cast<std::add_pointer_t<vertexType>>(stagingBufferMemory.mapMemory(0, bufferSize));
	std::ranges::uninitialized_copy(vertices, std::span{data, vertices.size()});
	stagingBufferMemory.unmapMemory();

	auto [retVertBuffer, retVertBufferMemory] = makeBufferAndMemory(bufferSize,
	                                                                vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
	                                                                vk::MemoryPropertyFlagBits::eDeviceLocal);

	copyBuffer(stagingBuffer, retVertBuffer, bufferSize);
	return std::make_pair(std::move(retVertBuffer), std::move(retVertBufferMemory));
}

auto Application::makeIndexBuffer() const -> BufferAndMemory
{
	using vertexIndexType = decltype(vertexIndices)::value_type;

	auto const bufferSize{sizeof(vertexIndexType) * vertexIndices.size()};
	auto const [stagingBuffer, stagingBufferMemory] =
	    makeBufferAndMemory(bufferSize,
	                        vk::BufferUsageFlagBits::eTransferSrc,
	                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	auto const data = static_cast<std::add_pointer_t<vertexIndexType>>(stagingBufferMemory.mapMemory(0, bufferSize));
	std::ranges::uninitialized_copy(vertexIndices, std::span{data, vertexIndices.size()});
	stagingBufferMemory.unmapMemory();

	auto [retIndexBuffer, retIndexBufferMemory] = makeBufferAndMemory(bufferSize,
	                                                                  vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
	                                                                  vk::MemoryPropertyFlagBits::eDeviceLocal);
	copyBuffer(stagingBuffer, retIndexBuffer, bufferSize);

	return std::make_pair(std::move(retIndexBuffer), std::move(retIndexBufferMemory));
}

auto Application::makeUniformBuffers() const -> std::vector<BufferAndMemory>
{
	constexpr auto bufferSize            = sizeof(ModelViewProjection);
	auto           retBuffersAndMemories = std::vector<BufferAndMemory>{};
	retBuffersAndMemories.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::generate_n(std::back_inserter(retBuffersAndMemories),
	                        MAX_FRAMES_IN_FLIGHT,
	                        [this]
	                        {
		                        return makeBufferAndMemory(bufferSize,
		                                                   vk::BufferUsageFlagBits::eUniformBuffer,
		                                                   vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	                        });

	return retBuffersAndMemories;
}

auto Application::mapUniformBuffers() -> std::vector<void*>
{
	constexpr auto bufferSize = sizeof(ModelViewProjection);
	auto           retMaps    = std::vector<void*>{};
	retMaps.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::transform(uniformBuffersAndMemories,
	                       std::back_inserter(retMaps),
	                       [](auto const& bufferAndMemory) { return bufferAndMemory.second.mapMemory(0, bufferSize); });

	return retMaps;
}

auto Application::updateUniformBuffer(std::uint32_t const currentImage) const -> void
{
	static auto startTime   = std::chrono::high_resolution_clock::now();
	auto const  currentTime = std::chrono::high_resolution_clock::now();
	auto const  deltaTime   = std::chrono::duration<float, std::chrono::seconds::period>{currentTime - startTime};

	auto const  model = rotate(glm::mat4{1.0f}, deltaTime.count() * glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
	auto const  view  = lookAt(glm::vec3{2.0f}, {}, glm::vec3{0.0f, 0.0f, 1.0f});
	auto        projection =
	    glm::perspective(glm::radians(45.0f), static_cast<float>(swapchainExtent.width) / static_cast<float>(swapchainExtent.height), 0.1f, 10.0f);
	projection[1][1] *= -1;

	auto const mvproj = ModelViewProjection{model, view, projection};
	std::ranges::copy(std::span{&mvproj, 1}, static_cast<ModelViewProjection*>(uniformBuffersMaps[currentImage]));
}

auto Application::makeDescriptorPool() const -> vkr::DescriptorPool
{
	auto const uniformPoolSize = vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT};
	auto const samplerPoolSize = vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT};
	auto const poolSizes       = std::array{uniformPoolSize, samplerPoolSize};
	auto const poolInfo        = vk::DescriptorPoolCreateInfo{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, MAX_FRAMES_IN_FLIGHT, poolSizes};

	return logicalDevice.createDescriptorPool(poolInfo);
}

auto Application::makeDescriptorSets() -> vkr::DescriptorSets
{
	auto const layouts           = std::vector{MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout};
	auto const allocInfo         = vk::DescriptorSetAllocateInfo{*descriptorPool, layouts};
	auto       retDescriptorSets = vkr::DescriptorSets{logicalDevice, allocInfo};

	for (auto const i : rv::iota(0u, MAX_FRAMES_IN_FLIGHT)) {
		auto const bufferInfo            = vk::DescriptorBufferInfo{*uniformBuffersAndMemories.at(i).first, {}, sizeof(ModelViewProjection)};
		auto const imageInfo             = vk::DescriptorImageInfo{*textureSampler, *textureImageView, vk::ImageLayout::eReadOnlyOptimal};
		auto const bufferDescriptorWrite = vk::WriteDescriptorSet{*retDescriptorSets.at(i), 0, 0, vk::DescriptorType::eUniformBuffer, {}, bufferInfo};
		auto const imageDescriptorWrite =
		    vk::WriteDescriptorSet{*retDescriptorSets.at(i), 1, 0, vk::DescriptorType::eCombinedImageSampler, imageInfo};

		logicalDevice.updateDescriptorSets({bufferDescriptorWrite, imageDescriptorWrite}, {});
	}

	return retDescriptorSets;
}

auto Application::makeImageAndMemory(std::uint32_t const            width,
                                     std::uint32_t const            height,
                                     vk::Format const&              format,
                                     vk::ImageTiling const&         tiling,
                                     vk::ImageUsageFlags const&     usage,
                                     vk::MemoryPropertyFlags const& properties) const -> ImageAndMemory
{
	auto const imageInfo = vk::ImageCreateInfo{{},
	                                           vk::ImageType::e2D,
	                                           format,
	                                           vk::Extent3D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1u},
	                                           1u,
	                                           1u,
	                                           vk::SampleCountFlagBits::e1,
	                                           tiling,
	                                           usage,
	                                           vk::SharingMode::eExclusive,
	                                           {},
	                                           vk::ImageLayout::eUndefined};

	auto       image           = logicalDevice.createImage(imageInfo);
	auto const memRequirements = image.getMemoryRequirements();
	auto const allocInfo       = vk::MemoryAllocateInfo{memRequirements.size, findMemoryType(memRequirements.memoryTypeBits, properties)};
	auto       imageMemory     = logicalDevice.allocateMemory(allocInfo);
	image.bindMemory(*imageMemory, 0u);

	return std::make_pair(std::move(image), std::move(imageMemory));
}

auto Application::makeTextureImage(fs::path const& texturePath) const -> ImageAndMemory
{
	int        texWidth, texHeight, texChannels;
	auto const pixels    = stbi_load(texturePath.string().data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	auto const imageSize = static_cast<vk::DeviceSize>(texWidth) * texHeight * STBI_rgb_alpha;
	auto const pixelSpan = std::span{pixels, imageSize};

	if (pixels == nullptr) {
		throw std::runtime_error{std::format("Failed to load texture image: {}", texturePath.string())};
	}

	auto [stagingBuffer, stagingBufferMemory] =
	    makeBufferAndMemory(imageSize,
	                        vk::BufferUsageFlagBits::eTransferSrc,
	                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	auto const data = static_cast<stbi_uc*>(stagingBufferMemory.mapMemory(0, imageSize));
	std::ranges::uninitialized_copy(pixelSpan, std::span{data, imageSize});

	stagingBufferMemory.unmapMemory();
	stbi_image_free(pixels);

	auto [textureImage, textureImageMemory] = makeImageAndMemory(texWidth,
	                                                             texHeight,
	                                                             vk::Format::eR8G8B8A8Srgb,
	                                                             vk::ImageTiling::eOptimal,
	                                                             vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
	                                                             vk::MemoryPropertyFlagBits::eDeviceLocal);

	transitionImageLayout(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	copyBufferToImage(stagingBuffer, textureImage, static_cast<std::uint32_t>(texWidth), static_cast<std::uint32_t>(texHeight));
	transitionImageLayout(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	return std::make_pair(std::move(textureImage), std::move(textureImageMemory));
}

auto Application::beginSingleTimeCommands() const -> vkr::CommandBuffer
{
	auto const     allocInfo     = vk::CommandBufferAllocateInfo{*commandPool, vk::CommandBufferLevel::ePrimary, 1u};
	auto           commandBuffer = std::move(logicalDevice.allocateCommandBuffers(allocInfo).front());
	constexpr auto beginInfo     = vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};

	commandBuffer.begin(beginInfo);

	return commandBuffer;
}

auto Application::endSingleTimeCommands(vkr::CommandBuffer&& commandBuffer) const -> void
{
	commandBuffer.end();
	auto const submitInfo = vk::SubmitInfo{{}, {}, *commandBuffer, {}};
	graphicsQueue.submit(submitInfo, {});
	graphicsQueue.waitIdle();
}

auto Application::transitionImageLayout(vkr::Image const&      image,
                                        vk::Format const&      format,
                                        vk::ImageLayout const& oldLayout,
                                        vk::ImageLayout const& newLayout) const -> void
{
	constexpr static auto getMasksAndStages =
	    [](vk::ImageLayout const& oldLayout,
	       vk::ImageLayout const& newLayout) -> std::tuple<vk::AccessFlags, vk::AccessFlags, vk::PipelineStageFlags, vk::PipelineStageFlags>
	{
		if (oldLayout == vk::ImageLayout::eUndefined and newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
			return std::make_tuple(vk::AccessFlagBits::eNone,
			                       vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			                       vk::PipelineStageFlagBits::eTopOfPipe,
			                       vk::PipelineStageFlagBits::eEarlyFragmentTests);
		}
		if (oldLayout == vk::ImageLayout::eUndefined and newLayout == vk::ImageLayout::eTransferDstOptimal) {
			return std::make_tuple(vk::AccessFlagBits::eNone,
			                       vk::AccessFlagBits::eTransferWrite,
			                       vk::PipelineStageFlagBits::eTopOfPipe,
			                       vk::PipelineStageFlagBits::eTransfer);
		}
		if (oldLayout == vk::ImageLayout::eTransferDstOptimal and newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
			return std::make_tuple(vk::AccessFlagBits::eTransferWrite,
			                       vk::AccessFlagBits::eShaderRead,
			                       vk::PipelineStageFlagBits::eTransfer,
			                       vk::PipelineStageFlagBits::eFragmentShader);
		}
		throw std::invalid_argument{"Unsupported layout transition"};
	};

	static auto const getAspectMask = [&format](vk::ImageLayout const& newLayout) -> vk::ImageAspectFlags
	{
		if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
			if (hasStencilComponent(format)) {
				return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
			}
			return vk::ImageAspectFlagBits::eDepth;
		}
		return vk::ImageAspectFlagBits::eColor;
	};

	auto const [srcAccessMask, dstAccessMask, srcStage, dstStage] = getMasksAndStages(oldLayout, newLayout);

	auto       commandBuffer = beginSingleTimeCommands();
	auto const barrier       = vk::ImageMemoryBarrier{srcAccessMask,
                                                dstAccessMask,
                                                oldLayout,
                                                newLayout,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                *image,
                                                vk::ImageSubresourceRange{getAspectMask(newLayout), 0u, 1u, 0u, 1u}};

	commandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);

	endSingleTimeCommands(std::move(commandBuffer));
}

auto Application::copyBufferToImage(vkr::Buffer const& buffer, vkr::Image const& image, std::uint32_t width, std::uint32_t height) const -> void
{
	auto       commandBuffer = beginSingleTimeCommands();
	auto const region =
	    vk::BufferImageCopy{0u, 0u, 0u, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u}, {0, 0, 0}, {width, height, 1u}};

	commandBuffer.copyBufferToImage(*buffer, *image, vk::ImageLayout::eTransferDstOptimal, region);

	endSingleTimeCommands(std::move(commandBuffer));
}

auto Application::makeTextureImageView() const -> vkr::ImageView
{
	return makeImageView(*textureImageAndMemory.first, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor);
}

auto Application::makeTextureSampler() const -> vkr::Sampler
{
	auto const properties  = physicalDevice.getProperties();
	auto const samplerInfo = vk::SamplerCreateInfo{{},
	                                               vk::Filter::eLinear,
	                                               vk::Filter::eLinear,
	                                               vk::SamplerMipmapMode::eLinear,
	                                               vk::SamplerAddressMode::eRepeat,
	                                               vk::SamplerAddressMode::eRepeat,
	                                               vk::SamplerAddressMode::eRepeat,
	                                               0.0f,
	                                               VK_TRUE,
	                                               properties.limits.maxSamplerAnisotropy,
	                                               VK_FALSE,
	                                               vk::CompareOp::eAlways,
	                                               0.0f,
	                                               0.0f,
	                                               vk::BorderColor::eIntOpaqueBlack,
	                                               VK_FALSE};

	return logicalDevice.createSampler(samplerInfo);
}

auto Application::makeDepthImage() const -> ImageAndMemory
{
	auto const depthFormat              = findDepthFormat();
	auto [depthImage, depthImageMemory] = makeImageAndMemory(swapchainExtent.width,
	                                                         swapchainExtent.height,
	                                                         depthFormat,
	                                                         vk::ImageTiling::eOptimal,
	                                                         vk::ImageUsageFlagBits::eDepthStencilAttachment,
	                                                         vk::MemoryPropertyFlagBits::eDeviceLocal);

	transitionImageLayout(depthImage, depthFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

	return std::make_pair(std::move(depthImage), std::move(depthImageMemory));
}

auto Application::makeDepthImageView() const -> vkr::ImageView
{
	return makeImageView(*depthImageAndMemory.first, findDepthFormat(), vk::ImageAspectFlagBits::eDepth);
}

auto Application::findSupportedFormat(std::span<vk::Format const>   candidates,
                                      vk::ImageTiling const&        tiling,
                                      vk::FormatFeatureFlags const& features) const -> vk::Format
{
	if (auto const it = std::ranges::find_if(
	        candidates,
	        [&](auto const& props)
	        {
		        if (tiling == vk::ImageTiling::eLinear and (props.linearTilingFeatures & features) == features) {
			        return true;
		        }
		        if (tiling == vk::ImageTiling::eOptimal and (props.optimalTilingFeatures & features) == features) {
			        return true;
		        }
		        return false;
	        },
	        [&](auto const& format) { return physicalDevice.getFormatProperties(format); });
	    it != std::end(candidates))
	{
		return *it;
	}
	throw std::runtime_error{"Failed to find a supported format"};
}

auto Application::findDepthFormat() const -> vk::Format
{
	return findSupportedFormat(std::array{vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
	                           vk::ImageTiling::eOptimal,
	                           vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

Application::QueueFamilyIndices::QueueFamilyIndices(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
    : graphicsFamily{findGraphicsQueueFamilyIndex(physDev)},
      presentFamily{findPresentQueueFamilyIndex(physDev, surface)}
{}

auto Application::QueueFamilyIndices::findGraphicsQueueFamilyIndex(vkr::PhysicalDevice const& physDev) -> std::optional<std::uint32_t>
{
	auto const queueFamilyProps{physDev.getQueueFamilyProperties()};

	if (auto const it = std::ranges::find_if(queueFamilyProps,
	                                         [](auto const& queueFamily)
	                                         { return queueFamily.queueCount > 0 and queueFamily.queueFlags & vk::QueueFlagBits::eGraphics; });
	    it != std::end(queueFamilyProps))
	{
		return {std::distance(std::begin(queueFamilyProps), it)};
	}

	return {};
}

auto Application::QueueFamilyIndices::findPresentQueueFamilyIndex(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
    -> std::optional<std::uint32_t>
{
	auto const queueFamilyProps = physDev.getQueueFamilyProperties();

	for (auto i{0u}; i < queueFamilyProps.size(); ++i) {
		if (queueFamilyProps[i].queueCount > 0 and physDev.getSurfaceSupportKHR(i, *surface)) {
			return i;
		}
	}

	return {};
}

auto Application::QueueFamilyIndices::isComplete() const -> bool { return graphicsFamily.has_value() and presentFamily.has_value(); }

auto Application::QueueFamilyIndices::indices() const -> std::vector<std::uint32_t>
{
	static auto indices{std::vector<std::uint32_t>{}};
	indices.reserve(2);

	if (graphicsFamily.has_value()) {
		indices.push_back(*graphicsFamily);
	}
	if (presentFamily.has_value() and *presentFamily != *graphicsFamily) {
		indices.push_back(*presentFamily);
	}

	return indices;
}

Application::SwapchainSupportDetails::SwapchainSupportDetails(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
    : capabilities{physDev.getSurfaceCapabilitiesKHR(*surface)},
      formats{physDev.getSurfaceFormatsKHR(*surface)},
      presentModes{physDev.getSurfacePresentModesKHR(*surface)}
{}

auto Application::SwapchainSupportDetails::isAdequate() const -> bool { return not(formats.empty() or presentModes.empty()); }
}// namespace HelloTriangle
