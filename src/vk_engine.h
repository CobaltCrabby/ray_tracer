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

struct RayMaterial {
	alignas(16) glm::vec3 albedo;
	alignas(16) glm::vec3 emissionColor;
	alignas(4) float emissionStrength;
};

struct Material {
	VkDescriptorSet textureSet{VK_NULL_HANDLE};
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject {
	Mesh* mesh;
	Material* material;
	glm::mat4 transformMatrix;
	glm::vec3 color;
};

struct GPUCameraData {
	glm::mat4 proj;
	glm::mat4 view;
	glm::mat4 viewProj;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

struct GPUSceneData {
	glm::vec4 fogColor; // w is for exponent
	glm::vec4 fogDistances; //x for min, y for max, zw unused.
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; //w for sun power
	glm::vec4 sunlightColor;
};

struct GPUColorData {
	alignas(16) glm::vec3 color;
};

struct CamSceneData {
	GPUCameraData camera;
	GPUSceneData scene;
};

struct FrameData {
	VkSemaphore presentSemaphore, renderSemaphore, computeSemaphore;
	VkFence renderFence, computeFence;

	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;

	VkCommandPool computeCmdPool;
	VkCommandBuffer computeCmdBuffer;
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
	glm::mat4 cameraRotation;
	alignas(4) glm::vec3 pos = glm::vec3(0.f, -0.5f, -3.5f);
	float nearPlane = 0.1f;
	float aspectRatio;
	float fov = 50.f;
};

struct EnvironmentData {
	alignas(16) glm::vec3 horizonColor = glm::vec3(0.986f, 1.f, 0.902f);
	alignas(16) glm::vec3 zenithColor = glm::vec3(0.265f, 0.595f, 0.887);
	alignas(16) glm::vec3 groundColor = glm::vec3(0.431f);
	alignas(4) float sunFocus = 1000.f;
	alignas(4) float sunIntensity = 200.f;
};

struct RayTracerData {
	bool progressive = false;
	uint raysPerPixel = 10;
	uint bounceLimit = 10;
	uint sphereCount;
};

struct PushConstants {
	CameraInfo camInfo;
	EnvironmentData environment;
	RayTracerData rayTraceParams;
	alignas(16) glm::vec3 lightDir;
	uint sphereCount;
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
	void init_scene();
	void init_descriptors();
	void init_imgui();

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
	void load_meshes();
	void load_images();
	void upload_mesh(Mesh& mesh);
	void copy_buffer(size_t bufferSize, AllocatedBuffer& buffer, VkBufferUsageFlags flags, void* bufferData);
	void update_buffer(size_t bufferSize, AllocatedBuffer& buffer, void* bufferData);

	size_t pad_uniform_buffer_size(size_t originalSize);

	Material* create_material(VkPipeline pipeline, VkPipelineLayout pipelineLayout, const std::string& name);
	Material* get_material(const std::string& name);
	Mesh* get_mesh(const std::string& name);
	void dispatch_compute(VkQueue queue, VkCommandBuffer cmd);
	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);
	void imgui_draw();

	FrameData& get_current_frame();

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

	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;
	VkQueue computeQueue;
	uint32_t computeQueueFamily;

	VkRenderPass renderPass;
	std::vector<VkFramebuffer> framebuffers;

	FrameData frames[FRAME_OVERLAP];
	std::vector<Sphere> spheres;
	std::vector<RayMaterial> rayMaterials;

	VkDescriptorSet computeSet;
	VkDescriptorSetLayout singleTextureLayout;
	VkDescriptorSetLayout computeLayout;
	VkDescriptorPool descriptorPool;

	AllocatedBuffer sphereBuffer;
	VkDescriptorSet sphereDescriptor;
	AllocatedBuffer materialBuffer;
	VkDescriptorSet materialDescriptor;

	VkPipelineLayout computePipeLayout;
	VkPipeline computePipeline;

	VmaAllocator allocator;
	UploadContext uploadContext;

	std::vector<RenderObject> renderables;
	std::unordered_map<std::string, Material> materials;
	std::unordered_map<std::string, Mesh> meshes;
	std::unordered_map<std::string, Texture> textures;

	glm::vec3 cameraPos = {0.f, -4.f, -10.f};
	float cameraSpeed = 0.3f;

	bool _isInitialized{false};
	int _frameNumber{0};
	uint64_t _lastTime;

	float cameraAngles[3] = {4.f, 0.f, 0.f};
	RayTracerData rayTracerParams;
	RenderStats renderStats;
	CameraInfo cameraInfo;
	EnvironmentData environment;

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