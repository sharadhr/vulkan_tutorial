#pragma once

#include <GLFW/glfw3.h>
#include <cstdint>
#include <filesystem>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace HelloTriangle
{
class Application;
namespace vkr = vk::raii;

inline constexpr auto glfwWindowDestroyer{[](auto windowPtr)
                                          {
	                                          glfwDestroyWindow(windowPtr);
	                                          glfwTerminate();
                                          }};
using GLFWWindowPointer         = std::unique_ptr<GLFWwindow, decltype(glfwWindowDestroyer)>;
using PipelineLayoutAndPipeline = std::pair<vkr::PipelineLayout, vkr::Pipeline>;
using BufferAndMemory           = std::pair<vkr::Buffer, vkr::DeviceMemory>;
using ImageAndMemory            = std::pair<vkr::Image, vkr::DeviceMemory>;

auto makeWindowPointer(Application& app, std::uint32_t width = 800, std::uint32_t height = 600, std::string_view windowName = "empty")
    -> GLFWWindowPointer;

struct Vertex
{
	glm::vec3 position{};
	glm::vec3 colour{};
	glm::vec2 texCoord{};

	//

	static consteval auto getBindingDescription() -> vk::VertexInputBindingDescription { return {0, sizeof(Vertex)}; }

	static consteval auto getAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 3>
	{
		constexpr auto positionAttribute =
		    vk::VertexInputAttributeDescription{{}, {}, vk::Format::eR32G32B32Sfloat, static_cast<unsigned>(offsetof(Vertex, position))};
		constexpr auto colourAttribute =
		    vk::VertexInputAttributeDescription{1, {}, vk::Format::eR32G32B32Sfloat, static_cast<unsigned>(offsetof(Vertex, colour))};
		constexpr auto texCoordAttribute =
		    vk::VertexInputAttributeDescription{2u, 0u, vk::Format::eR32G32Sfloat, static_cast<unsigned>(offsetof(Vertex, texCoord))};

		return {positionAttribute, colourAttribute, texCoordAttribute};
	}
};

struct ModelViewProjection
{
	glm::mat4 model{};
	glm::mat4 view{};
	glm::mat4 projection{};
};

inline std::array     validationLayers{"VK_LAYER_KHRONOS_validation"};
inline std::array     requiredDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
inline constexpr auto INIT_WIDTH{800u};
inline constexpr auto INIT_HEIGHT{800u};
auto const            vertices{std::vector<Vertex>{{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                                                   {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                                                   {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                                   {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
                                                   // lower plane
                                                   {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                                                   {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                                                   {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                                   {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}}};
auto const            vertexIndices{std::vector<std::uint16_t>{0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4}};

class Application
{
public:
	//	CONSTRUCTORS, DESTRUCTORS
	Application();
	virtual ~Application();

	//	INSTANCE PUBLIC
	bool framebufferResized{};
	auto run() -> void;

	//	STATIC PUBLIC

	//	TYPES, TYPE ALIASES
	struct QueueFamilyIndices
	{
		std::optional<std::uint32_t> graphicsFamily{};
		std::optional<std::uint32_t> presentFamily{};

		QueueFamilyIndices(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface);
		[[nodiscard]] auto isComplete() const -> bool;
		[[nodiscard]] auto indices() const -> std::vector<std::uint32_t>;

	private:
		static auto findGraphicsQueueFamilyIndex(vkr::PhysicalDevice const& physDev) -> std::optional<uint32_t>;
		static auto findPresentQueueFamilyIndex(vkr::PhysicalDevice const& physDev, vkr::SurfaceKHR const& surface) -> std::optional<uint32_t>;
	};

	struct SwapchainSupportDetails
	{
		vk::SurfaceCapabilitiesKHR        capabilities{};
		std::vector<vk::SurfaceFormatKHR> formats{};
		std::vector<vk::PresentModeKHR>   presentModes{};

		SwapchainSupportDetails(vkr::PhysicalDevice const&, vkr::SurfaceKHR const&);
		[[nodiscard]] auto isAdequate() const -> bool;
		auto               operator<=>(SwapchainSupportDetails const&) const = default;
	};

private:
	// MEMBERS
	// miscellaneous stuff
#ifdef NDEBUG
	bool const enableValidationLayers{false};
#else
	bool const enableValidationLayers{true};
#endif

	std::uint32_t MAX_FRAMES_IN_FLIGHT{2u};
	std::string   windowName{"Hello Triangle"};
	std::string   applicationName{"Hello Triangle"};
	std::uint32_t applicationVersion{VK_MAKE_API_VERSION(0, 1, 0, 0)};
	std::string   engineName{"No engine"};
	std::uint32_t engineVersion{VK_MAKE_API_VERSION(0, 1, 0, 0)};

	// window
	GLFWWindowPointer window{makeWindowPointer(*this, INIT_WIDTH, INIT_HEIGHT, windowName)};

	// context, instance, surface
	vkr::Context  context{};
	vkr::Instance instance{makeInstance()};
#ifndef NDEBUG
	vkr::DebugUtilsMessengerEXT debugMessenger{makeDebugMessenger()};
#endif
	VkSurfaceKHR    sfc{};
	vkr::SurfaceKHR surface{makeSurface()};

	// device details
	vkr::PhysicalDevice      physicalDevice{pickPhysicalDevice()};
	QueueFamilyIndices const queueFamilyIndices{physicalDevice, surface};
	SwapchainSupportDetails  swapchainSupport{physicalDevice, surface};
	vkr::Device              logicalDevice{makeDevice()};

	// queues
	vkr::Queue graphicsQueue{logicalDevice.getQueue(queueFamilyIndices.graphicsFamily.value(), 0)};
	vkr::Queue presentQueue{logicalDevice.getQueue(queueFamilyIndices.presentFamily.value(), 0)};

	// swapchain details
	vkr::SwapchainKHR           swapchain{makeSwapchain()};
	vk::Format                  swapchainImageFormat{chooseSwapSurfaceFormat(swapchainSupport.formats).format};
	vk::Extent2D                swapchainExtent{chooseSwapExtent(window, swapchainSupport.capabilities)};
	std::vector<vkr::ImageView> swapchainImageViews{makeImageViews()};

	// render pass, pipeline
	vkr::RenderPass               renderPass{makeRenderPass()};
	vkr::DescriptorSetLayout      descriptorSetLayout{makeDescriptorSetLayout()};
	PipelineLayoutAndPipeline     layoutAndPipeline{makeGraphicsPipeline()};

	// command pool
	vkr::CommandPool commandPool{makeCommandPool()};

	// buffers, bound memories, images
	BufferAndMemory              vertexBufferAndMemory{makeVertexBuffer()};
	BufferAndMemory              indexBufferAndMemory{makeIndexBuffer()};
	ImageAndMemory               textureImageAndMemory{makeTextureImage(R"(..\..\src\textures\statue.jpg)")};
	ImageAndMemory               depthImageAndMemory{makeDepthImage()};
	vkr::ImageView               textureImageView{makeTextureImageView()};
	vkr::ImageView               depthImageView{makeDepthImageView()};
	vkr::Sampler                 textureSampler{makeTextureSampler()};
	std::vector<BufferAndMemory> uniformBuffersAndMemories{makeUniformBuffers()};
	std::vector<void*>           uniformBuffersMaps{mapUniformBuffers()};

	// framebuffer
	std::vector<vkr::Framebuffer> swapchainFramebuffers{makeFramebuffers()};

	// descriptor pool
	vkr::DescriptorPool             descriptorPool{makeDescriptorPool()};
	std::vector<vkr::DescriptorSet> descriptorSets{makeDescriptorSets()};

	// command buffers
	std::vector<vkr::CommandBuffer> commandBuffers{makeCommandBuffers()};

	// synchronisation
	std::vector<vkr::Semaphore> imageAvailableSemaphores{makeSemaphores()};
	std::vector<vkr::Semaphore> renderFinishedSemaphores{makeSemaphores()};
	std::vector<vkr::Fence>     inFlightFences{makeFences()};
	std::uint32_t               currentFrameIndex{0u};

	//  INSTANCE PRIVATE
	auto               mainLoop() -> void;
	auto               drawFrame() -> void;
	[[nodiscard]] auto makeInstance() const -> vkr::Instance;
	[[nodiscard]] auto makeDebugMessenger() const -> vkr::DebugUtilsMessengerEXT;
	auto               makeSurface() -> vkr::SurfaceKHR;
	auto               pickPhysicalDevice() -> vkr::PhysicalDevice;
	[[nodiscard]] auto makeDevice() const -> vkr::Device;
	auto               makeSwapchain() -> vkr::SwapchainKHR;
	[[nodiscard]] auto makeImageView(vk::Image const&, vk::Format const&, vk::ImageAspectFlags const&) const -> vkr::ImageView;
	auto               makeImageViews() -> std::vector<vkr::ImageView>;
	[[nodiscard]] auto makeShaderModule(std::span<std::byte const>) const -> vkr::ShaderModule;
	[[nodiscard]] auto makeRenderPass() const -> vkr::RenderPass;
	[[nodiscard]] auto makeDescriptorSetLayout() const -> vkr::DescriptorSetLayout;
	[[nodiscard]] auto makeGraphicsPipeline() const -> PipelineLayoutAndPipeline;
	auto               makeFramebuffers() -> std::vector<vkr::Framebuffer>;
	[[nodiscard]] auto makeCommandPool() const -> vkr::CommandPool;
	[[nodiscard]] auto makeCommandBuffers() const -> vkr::CommandBuffers;
	auto               recordCommandBuffer(vkr::CommandBuffer const&, std::uint32_t) -> void;
	[[nodiscard]] auto makeSemaphores() const -> std::vector<vkr::Semaphore>;
	[[nodiscard]] auto makeFences() const -> std::vector<vkr::Fence>;
	auto               remakeSwapchain() -> void;
	[[nodiscard]] auto findMemoryType(std::uint32_t, vk::MemoryPropertyFlags const&) const -> std::uint32_t;
	[[nodiscard]] auto makeBufferAndMemory(vk::DeviceSize, vk::BufferUsageFlags const&, vk::MemoryPropertyFlags const&) const -> BufferAndMemory;
	auto               copyBuffer(vkr::Buffer const&, vkr::Buffer const&, vk::DeviceSize) const -> void;
	[[nodiscard]] auto makeVertexBuffer() const -> BufferAndMemory;
	[[nodiscard]] auto makeIndexBuffer() const -> BufferAndMemory;
	[[nodiscard]] auto makeUniformBuffers() const -> std::vector<BufferAndMemory>;
	auto               mapUniformBuffers() -> std::vector<void*>;
	auto               updateUniformBuffer(std::uint32_t) const -> void;
	[[nodiscard]] auto makeDescriptorPool() const -> vkr::DescriptorPool;
	auto               makeDescriptorSets() -> vkr::DescriptorSets;
	[[nodiscard]] auto makeTextureImage(std::filesystem::path const&) const -> ImageAndMemory;
	[[nodiscard]] auto makeImageAndMemory(std::uint32_t,
	                                      std::uint32_t,
	                                      vk::Format const&,
	                                      vk::ImageTiling const&,
	                                      vk::ImageUsageFlags const&,
	                                      vk ::MemoryPropertyFlags const&) const -> ImageAndMemory;
	[[nodiscard]] auto beginSingleTimeCommands() const -> vkr::CommandBuffer;
	auto               endSingleTimeCommands(vkr::CommandBuffer&&) const -> void;
	auto               transitionImageLayout(vkr::Image const&, vk::Format const&, vk::ImageLayout const&, vk::ImageLayout const&) const -> void;
	auto               copyBufferToImage(vkr::Buffer const&, vkr::Image const&, std::uint32_t, std::uint32_t) const -> void;
	[[nodiscard]] auto makeTextureImageView() const -> vkr::ImageView;
	[[nodiscard]] auto makeTextureSampler() const -> vkr::Sampler;
	auto               makeDepthImage() const -> ImageAndMemory;
	[[nodiscard]] auto makeDepthImageView() const -> vkr::ImageView;
	[[nodiscard]] auto findSupportedFormat(std::span<vk::Format const>, vk::ImageTiling const&, vk::FormatFeatureFlags const&) const -> vk::Format;
	[[nodiscard]] auto findDepthFormat() const -> vk::Format;

	//	STATIC PRIVATE
	static auto chooseSwapSurfaceFormat(std::span<vk::SurfaceFormatKHR const>) -> vk::SurfaceFormatKHR;
	static auto chooseSwapExtent(GLFWWindowPointer const&, vk::SurfaceCapabilitiesKHR const&) -> vk::Extent2D;
	static auto readFile(std::filesystem::path const&) -> std::vector<std::byte>;
};
}// namespace HelloTriangle
