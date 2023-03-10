#define GLM_FORCE_RADIANS
#include "HelloTriangleApplication.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <numeric>
#include <set>
#include <utility>
#include <vulkan/vulkan_raii.hpp>

namespace HelloTriangle
{
namespace fs = std::filesystem;

using namespace std::string_view_literals;
using namespace fmt::literals;

namespace
{
auto getGLFWInstanceExtensions() -> std::vector<char const*>
{
	static auto       glfwExtensionCount{0u};
	static auto const glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};
	static auto const extensions{std::vector(glfwExtensions, glfwExtensions + glfwExtensionCount)};

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
	auto        requiredValLayersCopy{std::vector<std::string_view>{std::begin(requiredValLayers), std::end(requiredValLayers)}};

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
	static auto constexpr header = R"({severity} --- {type}:
	Message ID Name   = <{id}>
	Message ID Number = {number}
	message           = <{message}>
)"sv;
	auto out{fmt::memory_buffer{}};

	fmt::format_to(std::back_inserter(out),
	               header,
	               "severity"_a = to_string(messageSeverity),
	               "type"_a     = to_string(messageType),
	               "id"_a       = pCallbackData->pMessageIdName,
	               "number"_a   = pCallbackData->messageIdNumber,
	               "message"_a  = pCallbackData->pMessage);

	if (pCallbackData->queueLabelCount > 0) {
		static auto constexpr queueLabelsHeader{"\tQueue Labels:\n"sv};
		static auto constexpr queueLabel{"\t\tlabelName = <{labelName}>\n"sv};
		auto const labels{std::span{pCallbackData->pQueueLabels, pCallbackData->queueLabelCount}};

		fmt::format_to(std::back_inserter(out), queueLabelsHeader);
		std::ranges::for_each(labels,
		                      [&](auto const& label) { fmt::format_to(std::back_inserter(out), queueLabel, "labelName"_a = label.pLabelName); });
	}

	if (pCallbackData->cmdBufLabelCount > 0) {
		static auto constexpr commandBufferLabelsHeader{"\tCommand Buffer Labels:\n"sv};
		static auto constexpr commandBufferLabel{"\t\tlabelName = <{labelName}>\n"sv};
		auto const labels{std::span{pCallbackData->pCmdBufLabels, pCallbackData->cmdBufLabelCount}};

		fmt::format_to(std::back_inserter(out), commandBufferLabelsHeader);
		std::ranges::for_each(labels,
		                      [&](auto const& label)
		                      { fmt::format_to(std::back_inserter(out), commandBufferLabel, "labelName"_a = label.pLabelName); });
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
			               "objectType"_a   = vk::to_string(object.objectType),
			               "objectHandle"_a = object.objectHandle,
			               object.pObjectName ? "pObjectName"_a = std::string_view{object.pObjectName} : "pObjectName"_a = ""sv);
		}
	}

	fmt::print(stderr, "{}", fmt::to_string(out));
	return VK_FALSE;
}

auto makeDebugMessengerCreateInfoEXT() -> vk::DebugUtilsMessengerCreateInfoEXT
{
	static auto constexpr severityFlags{vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError};
	static auto constexpr typeFlags{vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
	                                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance};
	auto const debugCreateInfo{vk::DebugUtilsMessengerCreateInfoEXT{{},
	                                                                severityFlags,
	                                                                typeFlags,
	                                                                reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(&debugMessageFunc)}};

	return debugCreateInfo;
}

auto checkDeviceExtensionSupport(vkr::PhysicalDevice const& physDev, std::span<char const*> requiredExtensions) -> bool
{
	static auto availableDeviceExtensions{physDev.enumerateDeviceExtensionProperties()};
	auto        requiredExtensionsCopy{std::vector<std::string_view>{std::begin(requiredExtensions), std::end(requiredExtensions)}};

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
	}
	return indices.isComplete() and Application::SwapchainSupportDetails(physDev, surface).isAdequate();
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

auto frameBufferResizeCallback = [](GLFWwindow* window, [[maybe_unused]] int width, [[maybe_unused]] int height)
{
	const auto app{static_cast<Application*>(glfwGetWindowUserPointer(window))};
	app->framebufferResized = true;
};
}// namespace

auto makeWindowPointer(Application& app, std::uint32_t const width, std::uint32_t const height, std::string_view windowName) -> GLFWWindowPointer
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	auto windowPtr{GLFWWindowPointer{glfwCreateWindow(width, height, windowName.data(), nullptr, nullptr)}};
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
	auto const applicationInfo{vk::ApplicationInfo{applicationName.data(), applicationVersion, engineName.data(), engineVersion, VK_API_VERSION_1_3}};
	auto const debugCreateInfo{makeDebugMessengerCreateInfoEXT()};
	auto const extensions{getRequiredExtensions(enableValidationLayers)};

	if (enableValidationLayers) {
		auto const instanceCreateInfo{vk::InstanceCreateInfo{{}, &applicationInfo, validationLayers, extensions, &debugCreateInfo}};
		return context.createInstance(instanceCreateInfo);
	}

	auto const instanceCreateInfo{vk::InstanceCreateInfo{{}, &applicationInfo, {}, extensions}};
	return context.createInstance(instanceCreateInfo);
}

auto Application::makeDebugMessenger() const -> vkr::DebugUtilsMessengerEXT
{
	auto const debugCreateInfo{makeDebugMessengerCreateInfoEXT()};
	return instance.createDebugUtilsMessengerEXT(debugCreateInfo);
}

auto Application::makeSurface() -> vkr::SurfaceKHR
{
	if (auto const result{glfwCreateWindowSurface(*instance, window.get(), nullptr, &sfc)}; result != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface");
	}

	return {instance, sfc};
}

auto Application::pickPhysicalDevice() -> vkr::PhysicalDevice
{
	static auto const physicalDevices{vkr::PhysicalDevices(instance)};

	auto const        physDevice{std::ranges::find_if(
            physicalDevices,
            [this](auto&& physDev) { return isDeviceSuitable(std::forward<decltype(physDev)>(physDev), surface, requiredDeviceExtensions); })};
	if (physDevice == std::end(physicalDevices)) {
		throw std::runtime_error("failed to find a suitable GPU");
	}
	return *physDevice;
}

auto Application::makeDevice() const -> vkr::Device
{
	auto constexpr queuePriorities{std::array{1.0f}};
	auto queueCreateInfos{std::vector<vk::DeviceQueueCreateInfo>{}};
	auto uniqueQueueFamilies{std::set{queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()}};

	std::ranges::transform(uniqueQueueFamilies,
	                       std::back_inserter(queueCreateInfos),
	                       [&](auto const& queueFamily) -> vk::DeviceQueueCreateInfo {
		                       return {{}, queueFamily, queuePriorities};
	                       });

	auto constexpr deviceFeatures{vk::PhysicalDeviceFeatures{}};

	if (enableValidationLayers) {
		auto const deviceCreateInfo{vk::DeviceCreateInfo{{}, queueCreateInfos, validationLayers, requiredDeviceExtensions, &deviceFeatures}};
		return physicalDevice.createDevice(deviceCreateInfo);
	}

	auto const deviceCreateInfo{vk::DeviceCreateInfo({}, queueCreateInfos, {}, requiredDeviceExtensions, &deviceFeatures)};
	return physicalDevice.createDevice(deviceCreateInfo);
}

auto Application::chooseSwapSurfaceFormat(std::span<vk::SurfaceFormatKHR const> availableFormats) -> vk::SurfaceFormatKHR
{
	static auto const format{std::ranges::find_if(availableFormats,
	                                              [](auto const& availableFormat) {
		                                              return availableFormat.format == vk::Format::eB8G8R8A8Srgb and
		                                                     availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
	                                              })};

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

	static auto actualExtent{vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}};

	actualExtent.width  = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
	actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

	return actualExtent;
}

auto Application::makeSwapchain() -> vkr::SwapchainKHR
{
	auto const newSwapchainSupport{SwapchainSupportDetails{physicalDevice, surface}};
	swapchainSupport = newSwapchainSupport;

	auto const [format, colorSpace]{chooseSwapSurfaceFormat(swapchainSupport.formats)};
	auto const presentMode{chooseSwapPresentMode(swapchainSupport.presentModes)};
	auto const extent{chooseSwapExtent(window, swapchainSupport.capabilities)};
	auto const imageCount{std::max(swapchainSupport.capabilities.minImageCount + 1, swapchainSupport.capabilities.maxImageCount)};
	auto const indices = queueFamilyIndices.indices();
	auto const swapchainCreateInfo{vk::SwapchainCreateInfoKHR{
	        {},
	        *surface,
	        imageCount,
	        format,
	        colorSpace,
	        extent,
	        1,
	        vk::ImageUsageFlagBits::eColorAttachment,
	        queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
	        indices,
	        swapchainSupport.capabilities.currentTransform,
	        vk::CompositeAlphaFlagBitsKHR::eOpaque,
	        presentMode,
	        VK_TRUE}};

	if (format != swapchainImageFormat or extent != swapchainExtent) {
		swapchainImageFormat = format;
		swapchainExtent      = extent;
	}

	return logicalDevice.createSwapchainKHR(swapchainCreateInfo);
}

auto Application::makeImageViews() -> std::vector<vkr::ImageView>
{
	auto imageViews{std::vector<vkr::ImageView>{}};
	std::ranges::transform(swapchain.getImages(),
	                       std::back_inserter(imageViews),
	                       [this](auto const& image)
	                       {
		                       auto const imageCreateInfo{vk::ImageViewCreateInfo{{},
		                                                                          image,
		                                                                          vk::ImageViewType::e2D,
		                                                                          swapchainImageFormat,
		                                                                          {},
		                                                                          {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}};

		                       return logicalDevice.createImageView(imageCreateInfo);
	                       });
	return imageViews;
}

auto Application::readFile(fs::path const& filePath) -> std::vector<std::byte>
{
	auto file{std::ifstream{filePath, std::ios::in | std::ios::binary}};
	if (!file.is_open()) {
		throw std::runtime_error{"failed to open file"};
	}
	auto const fileSize{fs::file_size(filePath)};
	auto       buffer{std::vector<std::byte>(fileSize)};
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

	return buffer;
}

auto Application::makeShaderModule(std::span<std::byte const> shaderCode) const -> vkr::ShaderModule
{
	auto const shaderModuleCreateInfo{vk::ShaderModuleCreateInfo{{}, shaderCode.size(), reinterpret_cast<uint32_t const*>(shaderCode.data())}};

	return logicalDevice.createShaderModule(shaderModuleCreateInfo);
}

auto Application::makeRenderPass() const -> vkr::RenderPass
{
	auto const colourAttachment{vk::AttachmentDescription{{},
	                                                      swapchainImageFormat,
	                                                      vk::SampleCountFlagBits::e1,
	                                                      vk::AttachmentLoadOp::eClear,
	                                                      vk::AttachmentStoreOp::eStore,
	                                                      vk::AttachmentLoadOp::eDontCare,
	                                                      vk::AttachmentStoreOp::eDontCare,
	                                                      vk::ImageLayout::eUndefined,
	                                                      vk::ImageLayout::ePresentSrcKHR}};
	auto constexpr colourAttachmentRef{vk::AttachmentReference{{0}, vk::ImageLayout::eColorAttachmentOptimal}};
	auto const subpass{vk::SubpassDescription{{}, vk::PipelineBindPoint::eGraphics, {}, colourAttachmentRef}};
	auto constexpr dependency{vk::SubpassDependency{VK_SUBPASS_EXTERNAL,
	                                                {},
	                                                vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                                                vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                                                {},
	                                                vk::AccessFlagBits::eColorAttachmentWrite}};
	auto const renderPassInfo{vk::RenderPassCreateInfo{{}, colourAttachment, subpass, dependency}};

	return logicalDevice.createRenderPass(renderPassInfo);
}

auto Application::makeDescriptorSetLayout() const -> vkr::DescriptorSetLayout
{
	auto constexpr mvprojLayoutBinding{vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex}};
	auto const layoutInfo{vk::DescriptorSetLayoutCreateInfo{{}, mvprojLayoutBinding}};

	return logicalDevice.createDescriptorSetLayout(layoutInfo);
}

auto Application::makeGraphicsPipeline() const -> PipelineLayoutAndPipeline
{
	auto const vertShaderCode{readFile("triangle.vert.spv"sv)};
	auto const fragShaderCode{readFile("triangle.frag.spv"sv)};

	auto const vertShaderModule{makeShaderModule(vertShaderCode)};
	auto const fragShaderModule{makeShaderModule(fragShaderCode)};

	auto const vertShaderStageInfo{vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertShaderModule, "main"}};
	auto const fragShaderStageInfo{vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragShaderModule, "main"}};
	auto const shaderStages{std::array{vertShaderStageInfo, fragShaderStageInfo}};

	auto constexpr dynamicStates{std::array{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};
	auto const dynamicState{vk::PipelineDynamicStateCreateInfo{{}, dynamicStates}};

	auto constexpr bindingDescription{Vertex::getBindingDescription()};
	auto constexpr attributeDescription{Vertex::getAttributeDescriptions()};
	auto const vertexInputInfo{vk::PipelineVertexInputStateCreateInfo{{}, bindingDescription, attributeDescription}};

	auto constexpr inputAssembly{vk::PipelineInputAssemblyStateCreateInfo{{}, vk::PrimitiveTopology::eTriangleList}};
	auto const viewport{vk::Viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f}};

	auto const scissor{vk::Rect2D{{0, 0}, swapchainExtent}};
	auto const viewportState{vk::PipelineViewportStateCreateInfo{{}, 1u, &viewport, 1u, &scissor}};

	auto constexpr rasteriser{vk::PipelineRasterizationStateCreateInfo{{},
	                                                                   VK_FALSE,
	                                                                   VK_FALSE,
	                                                                   vk::PolygonMode::eFill,
	                                                                   vk::CullModeFlagBits::eBack,
	                                                                   vk::FrontFace::eCounterClockwise,
	                                                                   VK_FALSE,
	                                                                   {},
	                                                                   {},
	                                                                   {},
	                                                                   1.0f}};

	auto constexpr multisampling{vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1}};

	auto constexpr colourBlendAttachment{vk::PipelineColorBlendAttachmentState{
	        {},
	        vk::BlendFactor::eOne,
	        vk::BlendFactor::eOne,
	        {},
	        {},
	        {},
	        {},
	        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eA}};

	auto const colourBlending{vk::PipelineColorBlendStateCreateInfo{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &colourBlendAttachment}};

	auto const pipelineLayoutInfo{vk::PipelineLayoutCreateInfo{{}, *descriptorSetLayout}};
	auto       retPipelineLayout{logicalDevice.createPipelineLayout(pipelineLayoutInfo)};

	auto const pipelineInfo{vk::GraphicsPipelineCreateInfo{{},
	                                                       shaderStages,
	                                                       &vertexInputInfo,
	                                                       &inputAssembly,
	                                                       {},
	                                                       &viewportState,
	                                                       &rasteriser,
	                                                       &multisampling,
	                                                       {},
	                                                       &colourBlending,
	                                                       &dynamicState,
	                                                       *retPipelineLayout,
	                                                       *renderPass,
	                                                       0u,
	                                                       {},
	                                                       -1}};

	auto       retGraphicsPipeline{logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo)};

	return {std::move(retPipelineLayout), std::move(retGraphicsPipeline)};
}

auto Application::makeFramebuffers() -> std::vector<vkr::Framebuffer>
{
	auto framebuffers{std::vector<vkr::Framebuffer>{}};
	framebuffers.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::transform(
	        swapchainImageViews,
	        std::back_inserter(framebuffers),
	        [&](auto const& imageView)
	        {
		        auto const framebufferInfo{vk::FramebufferCreateInfo{{}, *renderPass, *imageView, swapchainExtent.width, swapchainExtent.height, 1u}};
		        return logicalDevice.createFramebuffer(framebufferInfo);
	        });

	return framebuffers;
}

auto Application::makeCommandPool() const -> vkr::CommandPool
{
	auto const poolInfo{vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndices.graphicsFamily.value()}};

	return logicalDevice.createCommandPool(poolInfo);
}

auto Application::makeCommandBuffers() const -> vkr::CommandBuffers
{
	auto const allocInfo{vk::CommandBufferAllocateInfo{*commandPool, vk::CommandBufferLevel::ePrimary, MAX_FRAMES_IN_FLIGHT}};

	return {logicalDevice, allocInfo};
}

auto Application::recordCommandBuffer(vkr::CommandBuffer const& commandBuffer, std::uint32_t const imageIndex) -> void
{
	auto constexpr beginInfo{vk::CommandBufferBeginInfo{}};
	commandBuffer.begin(beginInfo);

	auto constexpr clearColour{vk::ClearValue{{std::array{0.0f, 0.0f, 0.0f, 1.0f}}}};
	auto const renderPassInfo{vk::RenderPassBeginInfo{*renderPass, *swapchainFramebuffers.at(imageIndex), {{}, swapchainExtent}, clearColour}};

	commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *layoutAndPipeline.second);
	auto constexpr offset{vk::DeviceSize{0}};
	commandBuffer.bindVertexBuffers(0u, *vertexBufferAndMemory.first, offset);
	commandBuffer.bindIndexBuffer(*indexBufferAndMemory.first, 0, vk::IndexType::eUint16);

	auto const viewport{vk::Viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f}};
	commandBuffer.setViewport(0, viewport);
	auto const scissor{vk::Rect2D{{}, swapchainExtent}};
	commandBuffer.setScissor(0, scissor);

	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layoutAndPipeline.first, {}, *descriptorSets[currentFrameIndex], {});

	commandBuffer.drawIndexed(static_cast<uint32_t>(vertexIndices.size()), 1, 0, 0, 0);
	commandBuffer.endRenderPass();
	commandBuffer.end();
}

auto Application::drawFrame() -> void
{
	if (auto const waitResult{logicalDevice.waitForFences(*inFlightFences.at(currentFrameIndex), VK_TRUE, std::numeric_limits<std::uint64_t>::max())};
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

	auto const& waitSemaphores{*imageAvailableSemaphores.at(currentFrameIndex)};
	auto const& signalSemaphores{*renderFinishedSemaphores.at(currentFrameIndex)};
	auto const& submitCommandBuffers{*commandBuffers.at(currentFrameIndex)};
	auto constexpr waitStages{vk::Flags{vk::PipelineStageFlagBits::eColorAttachmentOutput}};

	updateUniformBuffer(currentFrameIndex);

	auto const submitInfo{vk::SubmitInfo{waitSemaphores, waitStages, submitCommandBuffers, signalSemaphores}};

	graphicsQueue.submit(submitInfo, *inFlightFences.at(currentFrameIndex));

	auto const presentInfo{vk::PresentInfoKHR{signalSemaphores, *swapchain, imageIndex}};

	if (auto const presentResult{presentQueue.presentKHR(presentInfo)}; presentResult == vk::Result::eSuboptimalKHR or framebufferResized) {
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
	auto constexpr semaphoreInfo{vk::SemaphoreCreateInfo{}};
	auto semaphores{std::vector<vkr::Semaphore>{}};
	semaphores.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::generate_n(std::back_inserter(semaphores), MAX_FRAMES_IN_FLIGHT, [&] { return vkr::Semaphore{logicalDevice, semaphoreInfo}; });
	return semaphores;
}

auto Application::makeFences() const -> std::vector<vkr::Fence>
{
	auto constexpr fenceInfo{vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled}};
	auto fences{std::vector<vkr::Fence>{}};
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
	for (auto const memProperties{physicalDevice.getMemoryProperties()}; auto const i : std::views::iota(0u, memProperties.memoryTypeCount)) {
		if (typeFilter & (1 << i) and (memProperties.memoryTypes.at(i).propertyFlags & flags) == flags) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type");
}

auto Application::makeBufferAndMemory(vk::DeviceSize const size, vk::BufferUsageFlags const& usage, vk::MemoryPropertyFlags const& properties) const
        -> BufferAndMemory
{
	auto const bufferInfo{vk::BufferCreateInfo{{}, size, usage}};
	auto       retBuffer{logicalDevice.createBuffer(bufferInfo)};

	auto const memoryRequirements{retBuffer.getMemoryRequirements()};
	auto const allocInfo{vk::MemoryAllocateInfo{memoryRequirements.size, findMemoryType(memoryRequirements.memoryTypeBits, properties)}};
	auto       retBufferMemory{logicalDevice.allocateMemory(allocInfo)};
	retBuffer.bindMemory(*retBufferMemory, 0);

	return std::make_pair(std::move(retBuffer), std::move(retBufferMemory));
}

auto Application::copyBuffer(vkr::Buffer const& srcBuffer, vkr::Buffer const& dstBuffer, vk::DeviceSize const size) const -> void
{
	auto const allocInfo{vk::CommandBufferAllocateInfo{*commandPool, vk::CommandBufferLevel::ePrimary, 1u}};
	auto const commandBuffer{std::move(logicalDevice.allocateCommandBuffers(allocInfo).front())};
	auto constexpr beginInfo{vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit}};
	auto const copyRegion{vk::BufferCopy{{}, {}, size}};

	commandBuffer.begin(beginInfo);
	commandBuffer.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);
	commandBuffer.end();
	auto const submitInfo{vk::SubmitInfo{{}, {}, *commandBuffer}};
	graphicsQueue.submit(submitInfo, VK_NULL_HANDLE);
	graphicsQueue.waitIdle();
}

auto Application::makeVertexBuffer() const -> std::pair<vkr::Buffer, vkr::DeviceMemory>
{
	auto const bufferSize{sizeof(decltype(vertices)::value_type) * vertices.size()};
	auto const [stagingBuffer,
	            stagingBufferMemory]{makeBufferAndMemory(bufferSize,
	                                                     vk::BufferUsageFlagBits::eTransferSrc,
	                                                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)};

	auto const data{stagingBufferMemory.mapMemory(0, bufferSize)};
	std::ranges::copy(vertices, static_cast<decltype(vertices)::value_type*>(data));
	stagingBufferMemory.unmapMemory();

	auto [retVertBuffer, retVertBufferMemory]{makeBufferAndMemory(bufferSize,
	                                                              vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
	                                                              vk::MemoryPropertyFlagBits::eDeviceLocal)};

	copyBuffer(stagingBuffer, retVertBuffer, bufferSize);
	return std::make_pair(std::move(retVertBuffer), std::move(retVertBufferMemory));
}

auto Application::makeIndexBuffer() const -> BufferAndMemory
{
	auto const bufferSize{sizeof(decltype(vertexIndices)::value_type) * vertexIndices.size()};
	auto const [stagingBuffer,
	            stagingBufferMemory]{makeBufferAndMemory(bufferSize,
	                                                     vk::BufferUsageFlagBits::eTransferSrc,
	                                                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)};

	auto const data{stagingBufferMemory.mapMemory(0, bufferSize)};
	std::ranges::copy(vertexIndices, static_cast<decltype(vertexIndices)::value_type*>(data));
	stagingBufferMemory.unmapMemory();

	auto [retIndexBuffer, retIndexBufferMemory]{makeBufferAndMemory(bufferSize,
	                                                                vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
	                                                                vk::MemoryPropertyFlagBits::eDeviceLocal)};

	copyBuffer(stagingBuffer, retIndexBuffer, bufferSize);
	return std::make_pair(std::move(retIndexBuffer), std::move(retIndexBufferMemory));
}

auto Application::makeUniformBuffers() const -> std::vector<BufferAndMemory>
{
	auto constexpr bufferSize{sizeof(ModelViewProjectionObject)};
	auto retBuffersAndMemories{std::vector<BufferAndMemory>{}};
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
	constexpr auto bufferSize{sizeof(ModelViewProjectionObject)};
	auto           retMaps{std::vector<void*>{}};
	retMaps.reserve(MAX_FRAMES_IN_FLIGHT);

	std::ranges::transform(uniformBuffersAndMemories,
	                       std::back_inserter(retMaps),
	                       [](auto const& bufferAndMemory) { return bufferAndMemory.second.mapMemory(0, bufferSize); });
	return retMaps;
}

auto Application::updateUniformBuffer(std::uint32_t const currentImage) const -> void
{
	static auto startTime{std::chrono::high_resolution_clock::now()};
	auto const  currentTime{std::chrono::high_resolution_clock::now()};
	auto const  deltaTime{std::chrono::duration<float, std::chrono::seconds::period>{currentTime - startTime}};

	auto const  model{rotate(glm::mat4(1.0f), deltaTime.count() * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))};
	auto const  view{lookAt(glm::vec3{2.0f, 2.0f, 2.0f}, glm::vec3{}, glm::vec3{0.0f, 1.0f, 0.0f})};
	auto        projection{glm::perspective(glm::radians(45.0f), swapchainExtent.width / static_cast<float>(swapchainExtent.height), 0.1f, 10.0f)};
	projection[1][1] *= -1;

	auto const mvproj{ModelViewProjectionObject{model, view, projection}};
	std::ranges::copy(std::span{&mvproj, 1}, static_cast<ModelViewProjectionObject*>(uniformBuffersMaps[currentImage]));
}

auto Application::makeDescriptorPool() const -> vkr::DescriptorPool
{
	auto const poolSize{vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT}};
	auto const poolInfo{vk::DescriptorPoolCreateInfo{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, MAX_FRAMES_IN_FLIGHT, poolSize}};
	return logicalDevice.createDescriptorPool(poolInfo);
}

auto Application::makeDescriptorSets() -> vkr::DescriptorSets
{
	auto const layouts{std::vector{MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout}};
	auto const allocInfo{vk::DescriptorSetAllocateInfo{*descriptorPool, layouts}};
	auto       retDescriptorSets{vkr::DescriptorSets{logicalDevice, allocInfo}};

	for (auto const i : std::views::iota(0u, MAX_FRAMES_IN_FLIGHT)) {
		auto const bufferInfo{vk::DescriptorBufferInfo{*uniformBuffersAndMemories.at(i).first, {}, sizeof(ModelViewProjectionObject)}};
		auto const descriptorWrite{vk::WriteDescriptorSet{*retDescriptorSets.at(i), 0u, 0u, vk::DescriptorType::eUniformBuffer, {}, bufferInfo}};
		logicalDevice.updateDescriptorSets(descriptorWrite, {});
	}
	return retDescriptorSets;
}

Application::QueueFamilyIndices::QueueFamilyIndices(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface)
    : graphicsFamily{findGraphicsQueueFamilyIndex(physDev)},
      presentFamily{findPresentQueueFamilyIndex(physDev, surface)}
{}

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
