﻿// vulkan_guide.h : Include file for standard system include files,
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
   	uint v0;
   	uint v1;
   	uint v2;
	uint frontOnly;
};

struct TrianglePoint {
	alignas(16) glm::vec4 position; //uv.x is position.w
	alignas(16) glm::vec4 normal; //uv.y is normal.w
};

struct RayMaterial {
	alignas(16) glm::vec3 albedo = glm::vec3(1.f); //w = reflectance
	alignas(16) glm::vec3 emissionColor = glm::vec3(0.f);
	alignas(4) float emissionStrength = 0.f;
	alignas(4) float reflectance = 0.f;
	alignas(4) float ior = -1.f;
	alignas(4) uint albedoIndex = -1;
	alignas(4) uint metalnessIndex = -1;
};

struct BoundingBox {
	glm::vec4 bounds[2];
};

struct RenderObject {
	alignas(16) glm::mat4 transformMatrix;
	alignas(4) uint smoothShade; //0 = off, non-zero = on (bool weird on glsl)
	alignas(4) uint bvhIndex;
	alignas(4) uint materialIndex;
};

struct ImGuiObject {
	std::string name;
	glm::vec3 position = glm::vec3(0.f);
	glm::vec3 rotation = glm::vec3(0.f);
	glm::vec3 scale = glm::vec3(1.f);
	bool frontOnly = false;
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
	alignas(16) glm::vec4 horizonColor = glm::vec4(0.986f, 1.f, 0.902f, 1000.f); //w = sunFocus;
	alignas(16) glm::vec4 zenithColor = glm::vec4(0.265f, 0.595f, 0.887f, 200.f); //w = sunIntensity
	alignas(16) glm::vec3 groundColor = glm::vec3(0.431f);
	alignas(16) glm::vec4 lightDir = glm::vec4(normalize(glm::vec3(2.f, 0.8f, -3.f)), 0.f); //w component = environment on
};

struct RayTracerData {
	alignas(4) bool progressive = false;
	alignas(4) int debug = -1;
	alignas(4) uint raysPerPixel = 1;
	alignas(4) uint bounceLimit = 2;
	alignas(4) uint sphereCount;
	alignas(4) uint objectCount;
	alignas(4) uint triangleCap = 50;
	alignas(4) uint boxCap = 200;
};

struct PushConstants {
	CameraInfo camInfo; //44 -> 64 (?)
	EnvironmentData environment; //52 -> 64
	RayTracerData rayTraceParams; //20 -> 32
	uint frameCount;
};	

struct RenderStats {
	float frameTime;
	float drawTime;
};

struct BVHNode {
	glm::vec2 boundsX, boundsY, boundsZ;
	uint index, triCount = 0;
	//if triCount == 0: index is a node index, else: index is a triangle index
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
	void read_obj(std::string filePath, ImGuiObject imGui, int material);
	void build_bvh(int size, int triIndex);
	void update_bvh_bounds(uint index);
	void subdivide_bvh(uint intex, uint depth);

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

	std::vector<BVHNode> bvhNodes;
	uint nodesUsed = 0;

	VkDescriptorSet computeSet;
	VkDescriptorSet graphicsSet;
	VkDescriptorSetLayout graphicsLayout;
	VkDescriptorSetLayout computeLayout;
	VkDescriptorPool descriptorPool;

	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indexBuffer;
	Texture computeImage;

	AllocatedBuffer sphereBuffer;
	AllocatedBuffer triPointBuffer;
	AllocatedBuffer materialBuffer;
	AllocatedBuffer triangleBuffer;
	AllocatedBuffer objectBuffer;
	AllocatedBuffer bvhBuffer;

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

	glm::vec2 prevMouseScroll = glm::vec2(0.f);
	float mouseSensitivity = 100; 
	bool clicking = false;
	float cameraSpeed = 10.f;

	VkExtent2D _windowExtent{1728, 1117};

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