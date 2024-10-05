// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <deque>
#include <functional>
#include <unordered_map>

#include <vk_mem_alloc.h>
#include <vk_mesh.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		for (auto i = deletors.rbegin(); i != deletors.rend(); i++) {
			(*i)();
		}

		deletors.clear();
	}
};

struct Sphere {
	alignas(16) glm::vec3 position;
	alignas(4) float radius;
	alignas(4) uint materialIndex;
};

struct Triangle {
   	alignas(16) glm::uvec3 indices;
};

struct TrianglePoint {
	alignas(16) glm::vec3 position;
	alignas(8) glm::vec2 uv;
	alignas(16) glm::vec3 normal;
};

struct RayMaterial {
	alignas(16) glm::vec3 albedo;
	alignas(16) glm::vec3 emissionColor;
	alignas(4) float emissionStrength;
	alignas(4) float reflectance;
	alignas(4) uint textureIndex;
};

struct BoundingBox {
	alignas(16) glm::vec4 bounds[2];
};

struct RenderObject {
	bool smoothShade;
	alignas(4) uint triangleStart;
	alignas(4) uint triangleCount;
	alignas(4) uint materialIndex;
	alignas(16) glm::mat4 transformMatrix;
	BoundingBox boundingBox;
};

struct ImGuiObject {
	std::string name;
	RenderObject object;
};

struct UploadContext {
	VkFence uploadFence;
	VkCommandPool uploadPool;
	VkCommandBuffer uploadBuffer;
};

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};

struct CameraInfo {
	alignas(16) glm::mat4 cameraRotation;
	alignas(16) glm::vec3 pos = glm::vec3(0.f, -0.5f, -3.5f);
	alignas(4) float nearPlane = 0.1f;
	alignas(4) float aspectRatio;
	alignas(4) float fov = 50.f;
};

struct EnvironmentData {
	alignas(16) glm::vec3 horizonColor = glm::vec3(0.986f, 1.f, 0.902f);
	alignas(16) glm::vec3 zenithColor = glm::vec3(0.265f, 0.595f, 0.887);
	alignas(16) glm::vec3 groundColor = glm::vec3(0.431f);
	alignas(4) float sunFocus = 1000.f;
	alignas(4) float sunIntensity = 200.f;
};

struct RayTracerData {
	alignas(4) bool progressive = false;
	alignas(4) uint raysPerPixel = 1;
	alignas(4) uint bounceLimit = 2;
	alignas(4) uint sphereCount;
	alignas(4) uint objectCount;
};

struct PushConstants {
	CameraInfo camInfo; //44 -> 64 (?)
	EnvironmentData environment; //52 -> 64
	RayTracerData rayTraceParams; //20 -> 32
	alignas(16) glm::vec3 lightDir;
	uint frameCount;
};	

struct RenderStats {
	float frameTime;
	float drawTime;
};

constexpr unsigned int FRAME_OVERLAP = 2;
const unsigned int MAX_MATERIALS = 10;
const unsigned int MAX_SPHERES = 10;

class VulkanEngine {
private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_default_renderpass();
	void init_framebuffers();
	void init_sync_structures();
	void init_pipelines();
	void init_descriptors();
	void init_imgui();
	void init_image();

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
	void generate_quad();
	void read_obj(std::string filePath, int offset);

	void prepare_storage_buffers();
	void update_descriptors();
	void copy_buffer(size_t bufferSize, AllocatedBuffer& buffer, VkBufferUsageFlags flags, void* bufferData);
	void update_buffer(size_t bufferSize, AllocatedBuffer& buffer, void* bufferData);

	void imgui_draw();
	void run_compute();
	void run_graphics(uint index);

public:
	DeletionQueue deletionQueue;

	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties gpuProperties;
	VkDevice device;
	VkSurfaceKHR surface;

	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;

	//queues
	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;
	VkQueue computeQueue;
	uint32_t computeQueueFamily;

	VkRenderPass renderPass;
	std::vector<VkFramebuffer> framebuffers;
	std::vector<VkCommandBuffer> drawCmdBuffers;
	VkCommandBuffer computeCmdBuffer;
	VkCommandPool commandPool;

	std::vector<Sphere> spheres;
	std::vector<RayMaterial> rayMaterials;
	std::vector<Texture> textures;
	std::vector<TrianglePoint> triPoints;
	std::vector<Triangle> triangles;
	std::vector<RenderObject> objects;
	std::vector<ImGuiObject> imGuiObjects;

	VkDescriptorSet computeSet;
	VkDescriptorSet graphicsSet;
	VkDescriptorSetLayout graphicsLayout;
	VkDescriptorSetLayout computeLayout;
	VkDescriptorPool descriptorPool;

	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indexBuffer;
	Texture computeImage;

	AllocatedBuffer sphereBuffer;
	VkDescriptorSet sphereDescriptor;
	AllocatedBuffer triPointBuffer;
	VkDescriptorSet triPointDescriptor;
	AllocatedBuffer materialBuffer;
	VkDescriptorSet materialDescriptor;
	AllocatedBuffer triangleBuffer;
	VkDescriptorSet triangleDescriptor;
	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;

	VkPipelineLayout graphicsPipelineLayout;
	VkPipeline graphicsPipeline;

	VkPipelineLayout computePipeLayout;
	VkPipeline computePipeline;

	VkSemaphore presentSemaphore, renderSemaphore, computeSemaphore, graphicsSemaphore;
	VkFence renderFence, computeFence;

	VmaAllocator allocator;
	UploadContext uploadContext;

	ImDrawData* imGuiDrawData;

	//all dont need
	bool _isInitialized{false};
	int _frameNumber{0};

	float cameraAngles[3] = {4.f, 0.f, 0.f};
	RayTracerData rayTracerParams;
	RenderStats renderStats;
	CameraInfo cameraInfo;
	EnvironmentData environment;
	PushConstants constants;

	VkExtent2D _windowExtent{1920, 1080};

	struct SDL_Window* _window{nullptr};

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memory);
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
};

class PipelineBuilder {
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	VkPipelineColorBlendAttachmentState _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineLayout _pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;

	VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};