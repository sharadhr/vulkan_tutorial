#pragma once

#include <GLFW/glfw3.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vulkan/vulkan_raii.hpp>

namespace HelloTriangle
{
namespace vkr = vk::raii;

constexpr inline auto glfwWindowDestroyer{[](auto windowPtr)
                                          {
	                                          glfwDestroyWindow(windowPtr);
	                                          glfwTerminate();
                                          }};
using GLFWWindowPointer = std::unique_ptr<GLFWwindow, decltype(glfwWindowDestroyer)>;

auto makeWindowPointer(std::uint32_t width = 800, std::uint32_t height = 600, std::string_view windowName = "empty") -> GLFWWindowPointer;

class Application
{
public:
	//	CONSTRUCTORS, DESTRUCTORS
	Application();
	virtual ~Application();

	//	INSTANCE PUBLIC
	auto run() -> void;

	//	STATIC PUBLIC

	//	TYPES, TYPE ALIASES
	struct QueueFamilyIndices
	{
		std::optional<std::uint32_t> graphicsFamily{};
		std::optional<std::uint32_t> presentFamily{};

		QueueFamilyIndices(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface);
		[[nodiscard]] auto isComplete() const -> bool;

	private:
		static auto findGraphicsQueueFamilyIndex(vkr::PhysicalDevice const& physDev) -> std::optional<uint32_t>;
		static auto findPresentQueueFamilyIndex(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface) -> std::optional<uint32_t>;
	};

	struct SwapchainSupportDetails
	{
		vk::SurfaceCapabilitiesKHR capabilities{};
		std::vector<vk::SurfaceFormatKHR> formats{};
		std::vector<vk::PresentModeKHR> presentModes{};

		SwapchainSupportDetails(vkr::PhysicalDevice const&, vkr::SurfaceKHR const&);
		[[nodiscard]] auto isAdequate() const -> bool;
	};

private:
	//	MEMBERS
	uint32_t const width{800u};
	uint32_t const height{600u};
	std::string windowName{"Hello Triangle"};
	std::string applicationName{"Hello Triangle"};
	std::uint32_t applicationVersion{VK_MAKE_API_VERSION(0, 1, 0, 0)};
	std::string engineName{"No engine"};
	std::uint32_t engineVersion{VK_MAKE_API_VERSION(0, 1, 0, 0)};
	GLFWWindowPointer window{HelloTriangle::makeWindowPointer(width, height, windowName.data())};
	vkr::Context context{};
	std::array<char const*, 1> requiredValLayers{"VK_LAYER_KHRONOS_validation"};
	std::array<char const*, 1> requiredDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef NDEBUG
	bool const enableValidationLayers{false};
#else
	bool const enableValidationLayers{true};
#endif
	vkr::Instance instance{makeInstance()};

#ifndef NDEBUG
	vkr::DebugUtilsMessengerEXT debugMessenger{makeDebugMessenger()};
#endif
	VkSurfaceKHR sfc{};
	vkr::SurfaceKHR surface{makeSurface()};
	vkr::PhysicalDevice physicalDevice{pickPhysicalDevice()};
	QueueFamilyIndices const indices{physicalDevice, surface};
	SwapchainSupportDetails swapchainSupport{physicalDevice, surface};
	vkr::Device logicalDevice{makeDevice()};
	vkr::Queue graphicsQueue{logicalDevice.getQueue(indices.graphicsFamily.value(), 0)};
	vkr::Queue presentQueue{logicalDevice.getQueue(indices.presentFamily.value(), 0)};
	vkr::SwapchainKHR swapchain{makeSwapchain()};
	vk::Format swapchainImageFormat{chooseSwapSurfaceFormat(swapchainSupport.formats).format};
	vk::Extent2D swapchainExtent{chooseSwapExtent(window, swapchainSupport.capabilities)};
	std::vector<vk::Image> swapchainImages{swapchain.getImages()};
	std::vector<vkr::ImageView> swapchainImageViews{getImageViews()};
	vkr::RenderPass renderPass{makeRenderPass()};
	vkr::PipelineLayout pipelineLayout{makeGraphicsPipeline().first};
	vkr::Pipeline graphicsPipeline{makeGraphicsPipeline().second};
	std::vector<vkr::Framebuffer> swapchainFramebuffers{makeFramebuffers()};
	vkr::CommandPool commandPool{makeCommandPool()};
	vkr::CommandBuffer commandBuffer{makeCommandBuffer()};
	vkr::Semaphore imageAvailableSemaphore{makeSemaphore()};
	vkr::Semaphore renderFinishedSemaphore{makeSemaphore()};
	vkr::Fence inFlightFence{makeFence()};

	//  INSTANCE PRIVATE
	auto mainLoop() -> void;
	auto drawFrame() -> void;
	auto makeInstance() -> vkr::Instance;
	auto makeDebugMessenger() -> vkr::DebugUtilsMessengerEXT;
	auto makeSurface() -> vkr::SurfaceKHR;
	auto pickPhysicalDevice() -> vkr::PhysicalDevice;
	auto makeDevice() -> vkr::Device;
	auto makeSwapchain() -> vkr::SwapchainKHR;
	auto getImageViews() -> std::vector<vkr::ImageView>;
	auto makeShaderModule(std::span<std::byte const>) -> vkr::ShaderModule;
	auto makeRenderPass() -> vkr::RenderPass;
	auto makeGraphicsPipeline() -> std::pair<vkr::PipelineLayout, vkr::Pipeline>;
	auto makeFramebuffers() -> std::vector<vkr::Framebuffer>;
	auto makeCommandPool() -> vkr::CommandPool;
	[[nodiscard]] auto makeCommandBuffer() const -> vkr::CommandBuffer;
	auto recordCommandBuffer(std::uint32_t) -> void;
    auto makeSemaphore() -> vkr::Semaphore;
	auto makeFence() -> vkr::Fence;

	//	STATIC PRIVATE
	static auto chooseSwapSurfaceFormat(std::span<vk::SurfaceFormatKHR const>) -> vk::SurfaceFormatKHR;
	static auto chooseSwapExtent(GLFWWindowPointer const& window, vk::SurfaceCapabilitiesKHR const& capabilities) -> vk::Extent2D;
	static auto readFile(std::filesystem::path const&) -> std::vector<std::byte>;
};
}// namespace HelloTriangle