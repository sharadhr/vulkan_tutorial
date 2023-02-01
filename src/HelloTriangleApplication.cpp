#include "HelloTriangleApplication.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <set>

namespace HelloTriangle
{
namespace fs = std::filesystem;

using namespace std::string_view_literals;
using namespace fmt::literals;

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

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugMessageFunc(vk::DebugUtilsMessageSeverityFlagBitsEXT const messageSeverity,
                                                  vk::DebugUtilsMessageTypeFlagsEXT const messageType,
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
	return VK_FALSE;
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

auto frameBufferResizeCallback = [](GLFWwindow* window, [[maybe_unused]] int width, [[maybe_unused]] int height)
{
	const auto app{static_cast<HelloTriangle::Application*>(glfwGetWindowUserPointer(window))};
	app->framebufferResized = true;
};
}// namespace

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
			if (framebufferResized) {
				framebufferResized = false;
			}
			remakeSwapchain();
		}
	}
	logicalDevice.waitIdle();
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
	if (auto const result{glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window.get(), nullptr, &sfc)}; result != VK_SUCCESS) {
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
	auto constexpr queuePriority{1.0f};
	auto queueCreateInfos{std::vector<vk::DeviceQueueCreateInfo>{}};
	auto uniqueQueueFamilies{std::set{indices.graphicsFamily.value(), indices.presentFamily.value()}};

	std::ranges::transform(uniqueQueueFamilies,
	                       std::back_inserter(queueCreateInfos),
	                       [&](auto const& queueFamily) -> vk::DeviceQueueCreateInfo
	                       { return {.queueFamilyIndex{queueFamily}, .queueCount{1}, .pQueuePriorities{&queuePriority}}; });

	auto constexpr deviceFeatures{vk::PhysicalDeviceFeatures{}};
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

	static auto actualExtent{vk::Extent2D{.width{static_cast<std::uint32_t>(width)}, .height{static_cast<std::uint32_t>(height)}}};

	actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
	actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

	return actualExtent;
}

auto Application::makeSwapchain() -> vkr::SwapchainKHR
{
	auto const newSwapchainSupport{SwapchainSupportDetails{physicalDevice, surface}};
	if (swapchainSupport != newSwapchainSupport) {
		swapchainSupport = newSwapchainSupport;
	}
	auto const [format, colorSpace]{chooseSwapSurfaceFormat(swapchainSupport.formats)};
	auto const presentMode{chooseSwapPresentMode(swapchainSupport.presentModes)};
	auto const extent{chooseSwapExtent(window, swapchainSupport.capabilities)};
	auto const imageCount{std::max(swapchainSupport.capabilities.minImageCount + 1, swapchainSupport.capabilities.maxImageCount)};
	auto const indicesVec{std::vector{indices.graphicsFamily.value(), indices.presentFamily.value()}};

	auto swapchainCreateInfo{vk::SwapchainCreateInfoKHR{
	        .surface{*surface},
	        .minImageCount{imageCount},
	        .imageFormat{format},
	        .imageColorSpace{colorSpace},
	        .imageExtent{extent},
	        .imageArrayLayers{1},
	        .imageUsage{vk::ImageUsageFlagBits::eColorAttachment},
	        .imageSharingMode{indices.graphicsFamily != indices.presentFamily ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive},
	        .queueFamilyIndexCount{indices.graphicsFamily != indices.presentFamily ? static_cast<std::uint32_t>(indicesVec.size()) : 0u},
	        .pQueueFamilyIndices{indices.graphicsFamily != indices.presentFamily ? indicesVec.data() : nullptr},
	        .preTransform{swapchainSupport.capabilities.currentTransform},
	        .compositeAlpha{vk::CompositeAlphaFlagBitsKHR::eOpaque},
	        .presentMode{presentMode},
	        .clipped{VK_TRUE},
	        .oldSwapchain{VK_NULL_HANDLE}}};

	if (format != swapchainImageFormat or extent != swapchainExtent) {
		swapchainImageFormat = format;
		swapchainExtent = extent;
	}

	return {logicalDevice, swapchainCreateInfo};
}

auto Application::makeImageViews() -> std::vector<vkr::ImageView>
{
	auto imageViews{std::vector<vkr::ImageView>{}};
	std::ranges::transform(swapchain.getImages(),
	                       std::back_inserter(imageViews),
	                       [this](auto const& image)
	                       {
		                       auto const imageCreateInfo{vk::ImageViewCreateInfo{.image{image},
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

auto Application::readFile(fs::path const& filePath) -> std::vector<std::byte>
{
	auto file{std::ifstream{filePath, std::ios::in | std::ios::binary}};
	if (!file.is_open()) {
		throw std::runtime_error{"failed to open file"};
	}
	auto const fileSize{fs::file_size(filePath)};
	auto buffer{std::vector<std::byte>(fileSize)};
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

	return buffer;
}

auto Application::makeShaderModule(std::span<std::byte const> shaderCode) -> vkr::ShaderModule
{
	auto const shaderModuleCreateInfo{
	        vk::ShaderModuleCreateInfo{.codeSize{shaderCode.size()}, .pCode{reinterpret_cast<uint32_t const*>(shaderCode.data())}}};

	return {logicalDevice, shaderModuleCreateInfo};
}

auto Application::makeRenderPass() -> vkr::RenderPass
{
	auto const colourAttachment{vk::AttachmentDescription{.format{swapchainImageFormat},
	                                                      .samples{vk::SampleCountFlagBits::e1},
	                                                      .loadOp{vk::AttachmentLoadOp::eClear},
	                                                      .storeOp{vk::AttachmentStoreOp::eStore},
	                                                      .stencilLoadOp{vk::AttachmentLoadOp::eDontCare},
	                                                      .stencilStoreOp{vk::AttachmentStoreOp::eDontCare},
	                                                      .initialLayout{vk::ImageLayout::eUndefined},
	                                                      .finalLayout{vk::ImageLayout::ePresentSrcKHR}}};
	auto constexpr colourAttachmentRef{vk::AttachmentReference{.layout{vk::ImageLayout::eAttachmentOptimal}}};
	auto const subpass{vk::SubpassDescription{.pipelineBindPoint{vk::PipelineBindPoint::eGraphics},
	                                          .colorAttachmentCount{1u},
	                                          .pColorAttachments{&colourAttachmentRef}}};
	auto constexpr dependency{vk::SubpassDependency{.srcSubpass{VK_SUBPASS_EXTERNAL},
	                                                .srcStageMask{vk::PipelineStageFlagBits::eColorAttachmentOutput},
	                                                .dstStageMask{vk::PipelineStageFlagBits::eColorAttachmentOutput},
	                                                .dstAccessMask = {vk::AccessFlagBits::eColorAttachmentWrite}}};
	auto const renderPassInfo{vk::RenderPassCreateInfo{.attachmentCount{1u},
	                                                   .pAttachments{&colourAttachment},
	                                                   .subpassCount{1u},
	                                                   .pSubpasses{&subpass},
	                                                   .dependencyCount{1u},
	                                                   .pDependencies{&dependency}}};

	return {logicalDevice, renderPassInfo};
}

auto Application::makeGraphicsPipeline() -> std::pair<vkr::PipelineLayout, vkr::Pipeline>
{
	auto const vertShaderCode{readFile("triangle.vert.spv"sv)};
	auto const fragShaderCode{readFile("triangle.frag.spv"sv)};

	auto const vertShaderModule{makeShaderModule(vertShaderCode)};
	auto const fragShaderModule{makeShaderModule(fragShaderCode)};

	auto const vertShaderStageInfo{
	        vk::PipelineShaderStageCreateInfo{.stage{vk::ShaderStageFlagBits::eVertex}, .module{*vertShaderModule}, .pName{"main"}}};
	auto const fragShaderStageInfo{
	        vk::PipelineShaderStageCreateInfo{.stage{vk::ShaderStageFlagBits::eFragment}, .module{*fragShaderModule}, .pName{"main"}}};

	auto const shaderStages{std::array<vk::PipelineShaderStageCreateInfo, 2>{vertShaderStageInfo, fragShaderStageInfo}};
	auto const dynamicStates{std::vector<vk::DynamicState>{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};

	auto constexpr vertexInputInfo{vk::PipelineVertexInputStateCreateInfo{}};
	auto constexpr inputAssembly{
	        vk::PipelineInputAssemblyStateCreateInfo{.topology{vk::PrimitiveTopology::eTriangleList}, .primitiveRestartEnable{VK_FALSE}}};
	auto const viewport{vk::Viewport{.x{0.0f},
	                                 .y{0.0f},
	                                 .width{static_cast<float>(swapchainExtent.width)},
	                                 .height{static_cast<float>(swapchainExtent.height)},
	                                 .minDepth{0.0f},
	                                 .maxDepth{1.0f}}};

	auto const scissor{vk::Rect2D{.offset{0, 0}, .extent{swapchainExtent}}};
	auto const dynamicState{vk::PipelineDynamicStateCreateInfo{.dynamicStateCount{static_cast<std::uint32_t>(dynamicStates.size())},
	                                                           .pDynamicStates{dynamicStates.data()}}};
	auto const viewportState{
	        vk::PipelineViewportStateCreateInfo{.viewportCount{1u}, .pViewports{&viewport}, .scissorCount{1u}, .pScissors{&scissor}}};

	auto constexpr rasteriser{vk::PipelineRasterizationStateCreateInfo{.depthClampEnable{VK_FALSE},
	                                                                   .rasterizerDiscardEnable{VK_FALSE},
	                                                                   .polygonMode{vk::PolygonMode::eFill},
	                                                                   .cullMode{vk::CullModeFlagBits::eBack},
	                                                                   .frontFace{vk::FrontFace::eClockwise},
	                                                                   .depthBiasEnable{VK_FALSE},
	                                                                   .lineWidth{1.0f}}};

	auto constexpr multisampling{
	        vk::PipelineMultisampleStateCreateInfo{.rasterizationSamples{vk::SampleCountFlagBits::e1}, .sampleShadingEnable{VK_FALSE}}};

	auto constexpr colourBlendAttachment{
	        vk::PipelineColorBlendAttachmentState{.blendEnable{VK_FALSE},
	                                              .srcColorBlendFactor{vk::BlendFactor::eOne},
	                                              .srcAlphaBlendFactor{vk::BlendFactor::eOne},
	                                              .colorWriteMask{vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eB |
	                                                              vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eA}}};

	auto const colourBlending{vk::PipelineColorBlendStateCreateInfo{.logicOpEnable{VK_FALSE},
	                                                                .logicOp{vk::LogicOp::eCopy},
	                                                                .attachmentCount{1u},
	                                                                .pAttachments{&colourBlendAttachment}}};

	auto constexpr pipelineLayoutInfo{vk::PipelineLayoutCreateInfo{}};
	auto pipelineLayoutRet{vkr::PipelineLayout{logicalDevice, pipelineLayoutInfo}};

	auto const pipelineInfo{vk::GraphicsPipelineCreateInfo{.stageCount{2u},
	                                                       .pStages{shaderStages.data()},
	                                                       .pVertexInputState{&vertexInputInfo},
	                                                       .pInputAssemblyState{&inputAssembly},
	                                                       .pViewportState{&viewportState},
	                                                       .pRasterizationState{&rasteriser},
	                                                       .pMultisampleState{&multisampling},
	                                                       .pColorBlendState{&colourBlending},
	                                                       .pDynamicState{&dynamicState},
	                                                       .layout{*pipelineLayoutRet},
	                                                       .renderPass{*renderPass},
	                                                       .subpass{0u},
	                                                       .basePipelineIndex{-1}}};

	return {std::move(pipelineLayoutRet), vkr::Pipeline{logicalDevice, nullptr, pipelineInfo}};
}

auto Application::makeFramebuffers() -> std::vector<vkr::Framebuffer>
{
	std::vector<vkr::Framebuffer> framebuffers;

	std::ranges::transform(swapchainImageViews,
	                       std::back_inserter(framebuffers),
	                       [&](auto const& imageView)
	                       {
		                       auto const framebufferInfo{vk::FramebufferCreateInfo{.renderPass{*renderPass},
		                                                                            .attachmentCount{1u},
		                                                                            .pAttachments{&(*imageView)},
		                                                                            .width{swapchainExtent.width},
		                                                                            .height{swapchainExtent.height},
		                                                                            .layers{1u}}};

		                       return vkr::Framebuffer{logicalDevice, framebufferInfo};
	                       });

	return framebuffers;
}

auto Application::makeCommandPool() -> vkr::CommandPool
{
	auto const poolInfo{
	        vk::CommandPoolCreateInfo{.flags{vk::CommandPoolCreateFlagBits::eResetCommandBuffer}, .queueFamilyIndex{indices.graphicsFamily.value()}}};

	return {logicalDevice, poolInfo};
}

auto Application::makeCommandBuffers() const -> vkr::CommandBuffers
{
	auto const allocInfo{vk::CommandBufferAllocateInfo{.commandPool{*commandPool},
	                                                   .level{vk::CommandBufferLevel::ePrimary},
	                                                   .commandBufferCount{MAX_FRAMES_IN_FLIGHT}}};

	return {logicalDevice, allocInfo};
}

auto Application::recordCommandBuffer(vkr::CommandBuffer& commandBuffer, std::uint32_t imageIndex) -> void
{
	auto constexpr beginInfo{vk::CommandBufferBeginInfo{}};
	auto constexpr clearColour{vk::ClearValue{{std::array{0.0f, 0.0f, 0.0f, 1.0f}}}};
	commandBuffer.begin(beginInfo);

	auto const renderPassInfo{vk::RenderPassBeginInfo{.renderPass{*renderPass},
	                                                  .framebuffer{*swapchainFramebuffers.at(imageIndex)},
	                                                  .renderArea{.offset{0, 0}, .extent{swapchainExtent}},
	                                                  .clearValueCount{1},
	                                                  .pClearValues{&clearColour}}};

	commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);

	auto const viewport{vk::Viewport{.x{0.0f},
	                                 .y{0.0f},
	                                 .width{static_cast<float>(swapchainExtent.width)},
	                                 .height{static_cast<float>(swapchainExtent.height)},
	                                 .minDepth{0.0f},
	                                 .maxDepth{1.0f}}};
	commandBuffer.setViewport(0, viewport);

	auto const scissor{vk::Rect2D{.offset{0, 0}, .extent{swapchainExtent}}};
	commandBuffer.setScissor(0, scissor);
	commandBuffer.draw(3, 1, 0, 0);
	commandBuffer.endRenderPass();
	commandBuffer.end();
}

auto Application::drawFrame() -> void
{
	if (auto const waitResult{logicalDevice.waitForFences(*inFlightFences.at(currentFrame), VK_TRUE, std::numeric_limits<std::uint64_t>::max())};
	    waitResult != vk::Result::eSuccess)
	{
		throw std::runtime_error("failed to acquire swapchain image");
	}

	auto const [acquireResult, imageIndex] =
	        swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *imageAvailableSemaphores.at(currentFrame));
	if (acquireResult != vk::Result::eSuccess and acquireResult != vk::Result::eSuboptimalKHR) {
		throw std::runtime_error{"Failed to acquire swapchain image"};
	}

	logicalDevice.resetFences(*inFlightFences.at(currentFrame));

	commandBuffers.at(currentFrame).reset();
	recordCommandBuffer(commandBuffers.at(currentFrame), imageIndex);

	auto const waitSemaphores{std::array{*imageAvailableSemaphores.at(currentFrame)}};
	auto const signalSemaphores{std::array{*renderFinishedSemaphores.at(currentFrame)}};
	auto constexpr waitStages{vk::Flags{vk::PipelineStageFlagBits::eColorAttachmentOutput}};
	auto const submitInfo{vk::SubmitInfo{.waitSemaphoreCount{waitSemaphores.size()},
	                                     .pWaitSemaphores{waitSemaphores.data()},
	                                     .pWaitDstStageMask{&waitStages},
	                                     .commandBufferCount{1u},
	                                     .pCommandBuffers{&(*commandBuffers.at(currentFrame))},
	                                     .signalSemaphoreCount{signalSemaphores.size()},
	                                     .pSignalSemaphores{signalSemaphores.data()}}};
	graphicsQueue.submit(submitInfo, *inFlightFences.at(currentFrame));

	auto const swapChains{std::array{*swapchain}};
	auto const presentInfo{vk::PresentInfoKHR{.waitSemaphoreCount{signalSemaphores.size()},
	                                          .pWaitSemaphores{signalSemaphores.data()},
	                                          .swapchainCount{swapChains.size()},
	                                          .pSwapchains{swapChains.data()},
	                                          .pImageIndices{&imageIndex}}};

	if (auto const presentResult{presentQueue.presentKHR(presentInfo)}; presentResult == vk::Result::eSuboptimalKHR or framebufferResized) {
		framebufferResized = false;
		remakeSwapchain();
	} else if (presentResult != vk::Result::eSuccess) {
		throw std::runtime_error("failed to present swapchain image");
	}

	++currentFrame;
	currentFrame %= MAX_FRAMES_IN_FLIGHT;
}

auto Application::makeSemaphores() -> std::vector<vkr::Semaphore>
{
	auto constexpr semaphoreInfo{vk::SemaphoreCreateInfo{}};
	std::vector<vkr::Semaphore> semaphores;

	for (auto i{0u}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		semaphores.emplace_back(logicalDevice, semaphoreInfo);
	}

	return semaphores;
}

auto Application::makeFences() -> std::vector<vkr::Fence>
{
	auto constexpr fenceInfo{vk::FenceCreateInfo{.flags{vk::FenceCreateFlagBits::eSignaled}}};
	std::vector<vkr::Fence> fences;

	for (auto i{0u}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		fences.emplace_back(logicalDevice, fenceInfo);
	}

	return fences;
}

auto Application::remakeSwapchain() -> void
{
	auto width{0};
	auto height{0};
	glfwGetFramebufferSize(window.get(), &width, &height);
	while (width == 0 or height == 0) {
		glfwGetFramebufferSize(window.get(), &width, &height);
		glfwWaitEvents();
	}

	logicalDevice.waitIdle();

	swapchainFramebuffers.clear();
	swapchainImageViews.clear();
	swapchain.clear();

	swapchain = makeSwapchain();
	swapchainImageViews = makeImageViews();
	swapchainFramebuffers = makeFramebuffers();
}

auto Application::makeWindowPointer(const std::uint32_t width, const std::uint32_t height, std::string_view windowName) -> GLFWWindowPointer
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	auto windowPtr{GLFWWindowPointer{glfwCreateWindow(width, height, windowName.data(), nullptr, nullptr)}};
	glfwSetWindowUserPointer(windowPtr.get(), this);
	glfwSetFramebufferSizeCallback(windowPtr.get(), frameBufferResizeCallback);

	return windowPtr;
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
