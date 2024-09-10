
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include <iostream>
#include <fstream>

#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
using namespace std;
#define VK_CHECK(x)\
	do {\
		VkResult err = x;\
		if (err) {\
			std::cout << "Error detected" << err << endl;\
			abort();\
		}\
	} while (0)

void VulkanEngine::init() {
	// We initialize SDL and create a window with it.
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags);

	init_vulkan();
	init_swapchain();
	init_commands();
	init_default_renderpass();
	init_framebuffers();
	init_sync_structures();
	init_descriptors();
	init_pipelines();

	load_meshes();

	init_scene();

	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_vulkan() {
	// build instance
	vkb::InstanceBuilder builder;
	auto build = builder.set_app_name("Vulkan Application")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.enable_extension("VK_KHR_portability_enumeration")
		.build();

	vkb::Instance vkb_inst = build.value();
	instance = vkb_inst.instance;
	debugMessenger = vkb_inst.debug_messenger;

	//pick gpu
	SDL_Vulkan_CreateSurface(_window, instance, &surface);

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 1)
		.set_surface(surface)
		.select()
		.value();

	// build logical device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features{};
	shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETER_FEATURES;
	shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&shader_draw_parameters_features).build().value();

	device = vkbDevice.device;
	this->physicalDevice = physicalDevice.physical_device;
	gpuProperties = vkbDevice.physical_device.properties;

	cout << gpuProperties.limits.minUniformBufferOffsetAlignment << endl;

	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// create memory allocator
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.device = device;
	allocatorInfo.physicalDevice = physicalDevice.physical_device;
	allocatorInfo.instance = instance;
	vmaCreateAllocator(&allocatorInfo, &allocator);
}

void VulkanEngine::init_swapchain() {
	VkSurfaceFormatKHR surfaceFormat;
	surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::SwapchainBuilder builder{ physicalDevice, device, surface };
	vkb::Swapchain vkbSwapchain = builder.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.set_desired_format(surfaceFormat)
		.build()
		.value();

	this->swapchain = vkbSwapchain.swapchain;
	swapchainFormat = vkbSwapchain.image_format;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();

	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	depthFormat = VK_FORMAT_D32_SFLOAT;
	VkImageCreateInfo dimgInfo = vkinit::imageCreateInfo(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);
	
	VmaAllocationCreateInfo dimgAllocInfo{};
	dimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(allocator, &dimgInfo, &dimgAllocInfo, &depthImage.image, &depthImage.allocation, nullptr);

	VkImageViewCreateInfo dimgViewInfo = vkinit::imageViewCreateInfo(depthFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
	VK_CHECK(vkCreateImageView(device, &dimgViewInfo, nullptr, &depthImageView));

	deletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);
	});
}

void VulkanEngine::init_commands() {
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));

		VkCommandBufferAllocateInfo commandBufferInfo = vkinit::commandBufferAllocateInfo(frames[i].commandPool);
		VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferInfo, &frames[i].commandBuffer));

		deletionQueue.push_function([=]() {
			vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
		});
	}

	VkCommandPoolCreateInfo uploadPoolInfo = vkinit::commandPoolCreateInfo(graphicsQueueFamily);
	VK_CHECK(vkCreateCommandPool(device, &uploadPoolInfo, nullptr, &uploadContext.uploadPool));

	deletionQueue.push_function([=]() {
		vkDestroyCommandPool(device, uploadContext.uploadPool, nullptr);
	});

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(uploadContext.uploadPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &uploadContext.uploadBuffer));
}

void VulkanEngine::init_default_renderpass() {
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkSubpassDependency> dependencies;

	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapchainFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments.push_back(colorAttachment);

	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef{};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	attachments.push_back(depthAttachment);

	VkSubpassDescription subpass = {};
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	dependencies.push_back(dependency);

	VkSubpassDependency depthDependency = {};
	depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass = 0;
	depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = 0;
	depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	dependencies.push_back(depthDependency);

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = attachments.size();
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = dependencies.size();
	renderPassInfo.pDependencies = dependencies.data();

	VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
	deletionQueue.push_function([=]() {
		vkDestroyRenderPass(device, renderPass, nullptr);
	});
}

void VulkanEngine::init_framebuffers() {
	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.height = _windowExtent.height;
	framebufferInfo.width = _windowExtent.width;
	framebufferInfo.layers = 1;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

	const uint32_t imageCount = swapchainImages.size();
	framebuffers = vector<VkFramebuffer>(imageCount);

	for (int i = 0; i < imageCount; i++) {
		std::vector<VkImageView> attachments;
		attachments.push_back(swapchainImageViews[i]);
		attachments.push_back(depthImageView);

		framebufferInfo.attachmentCount = attachments.size();
		framebufferInfo.pAttachments = attachments.data();

		VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]));
		deletionQueue.push_function([=]() {
			vkDestroyFramebuffer(device, framebuffers[i], nullptr);
			vkDestroyImageView(device, swapchainImageViews[i], nullptr);
		});
	}	
}

void VulkanEngine::init_sync_structures() {
	VkFenceCreateInfo fenceInfo = vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreInfo = vkinit::semaphoreCreateInfo();

	VkFenceCreateInfo uploadFenceInfo = vkinit::fenceCreateInfo();
	VK_CHECK(vkCreateFence(device, &uploadFenceInfo, nullptr, &uploadContext.uploadFence));
	deletionQueue.push_function([=]() {
		vkDestroyFence(device, uploadContext.uploadFence, nullptr);
	});

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frames[i].renderFence));

		deletionQueue.push_function([=]() {
			vkDestroyFence(device, frames[i].renderFence, nullptr);
		});

		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].renderSemaphore));
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].presentSemaphore));

		deletionQueue.push_function([=]() {
			vkDestroySemaphore(device, frames[i].presentSemaphore, nullptr);
			vkDestroySemaphore(device, frames[i].renderSemaphore, nullptr);
		});
	}
}

void VulkanEngine::init_pipelines() {
	//load shader binaries
	std::string bin = std::filesystem::current_path().generic_string() + "/../shaders/bin/";
	VkShaderModule fragment;
	if (!load_shader_module((bin + "colored_triangle.frag.spv").c_str(), &fragment)) {
		cout << "error loading colored fragment shader" << endl;
	} else {
		cout << "successfully loaded colored fragment shader" << endl;
	}

	VkShaderModule vertex;
	if (!load_shader_module((bin + "colored_triangle.vert.spv").c_str(), &vertex)) {
		cout << "error loading colored vertex shader" << endl;
	} else {
		cout << "successfully loaded colored vertex shader" << endl;
	}

	VkShaderModule redFragment;
	if (!load_shader_module((bin + "triangle.frag.spv").c_str(), &redFragment)) {
		cout << "error loading fragment shader" << endl;
	} else {
		cout << "successfully loaded fragment shader" << endl;
	}

	VkShaderModule redVertex;
	if (!load_shader_module((bin + "triangle.vert.spv").c_str(), &redVertex)) {
		cout << "error loading vertex shader" << endl;
	} else {
		cout << "successfully loaded vertex shader" << endl;
	}

	VkShaderModule meshVertex;
	if (!load_shader_module((bin + "tri_mesh.vert.spv").c_str(), &meshVertex)) {
		std::cout << "error loading triangle vertex shader module" << std::endl;
	} else {
		std::cout << "successfully loaded red triangle vertex shader" << std::endl;
	}

	VkShaderModule litFrag;
	if (!load_shader_module((bin + "default_lit.frag.spv").c_str(), &litFrag)) {
		std::cout << "error loading lit frag shader module" << std::endl;
	} else {
		std::cout << "successfully loaded lit frag shader" << std::endl;
	}

	VkShaderModule combinedMeshVert;
	if (!load_shader_module((bin + "combined_mesh.vert.spv").c_str(), &combinedMeshVert)) {
		std::cout << "error loading combined mesh vert shader module" << std::endl;
	} else {
		std::cout << "successfully loaded combined mesh vert shader" << std::endl;
	}

	VkShaderModule combinedMeshFrag;
	if (!load_shader_module((bin + "combined_mesh.frag.spv").c_str(), &combinedMeshFrag)) {
		std::cout << "error loading combined mesh frag shader module" << std::endl;
	} else {
		std::cout << "successfully loaded combined mesh frag shader" << std::endl;
	}

	//create graphics pipeline
	PipelineBuilder builder;

	builder._vertexInputInfo = vkinit::pipelineVertexInputStateCreateInfo();
	builder._inputAssembly = vkinit::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	
	builder._viewport.x = 0.f;
	builder._viewport.y = 0.f;
	builder._viewport.width = (float)_windowExtent.width;
	builder._viewport.height = (float)_windowExtent.height;
	builder._viewport.minDepth = 0.f;
	builder._viewport.minDepth = 1.f;

	builder._scissor.offset = {0, 0};
	builder._scissor.extent = _windowExtent;

	builder._rasterizer = vkinit::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);
	builder._multisampling = vkinit::pipelineMultisampleStateCreateInfo();
	builder._colorBlendAttachment = vkinit::pipelineColorBlendAttachmentState();
	builder._depthStencil = vkinit::depthStencilCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);

	//mesh pipeline
	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	builder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	builder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
	builder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	builder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();

	VkPushConstantRange pushConstant;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(MeshPushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = {globalSetLayout, storageSetLayout};

	meshPipelineLayoutInfo.pSetLayouts = setLayouts;
	meshPipelineLayoutInfo.setLayoutCount = 2;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(device, &meshPipelineLayoutInfo, nullptr, &meshPipelineLayout));

	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, combinedMeshVert));
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, combinedMeshFrag));
	builder._pipelineLayout = meshPipelineLayout;

	create_material(builder.build_pipeline(device, renderPass), meshPipelineLayout, "defaultmesh");

	//can delete after pipeline creation
	vkDestroyShaderModule(device, fragment, nullptr);
	vkDestroyShaderModule(device, vertex, nullptr);
	vkDestroyShaderModule(device, redFragment, nullptr);
	vkDestroyShaderModule(device, redVertex, nullptr);
	vkDestroyShaderModule(device, meshVertex, nullptr);
	vkDestroyShaderModule(device, litFrag, nullptr);
	vkDestroyShaderModule(device, combinedMeshVert, nullptr);
	vkDestroyShaderModule(device, combinedMeshFrag, nullptr);

	deletionQueue.push_function([=]() {
		VkPipelineLayout lastLayout;
		for (const auto& [key, value] : materials) {
			vkDestroyPipeline(device, value.pipeline, nullptr);
			if (lastLayout != value.pipelineLayout) {
				vkDestroyPipelineLayout(device, value.pipelineLayout, nullptr);
				lastLayout = value.pipelineLayout;
			}
		}
    });
}

void VulkanEngine::init_scene() {
	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::mat4(1.f);
	monkey.color = glm::vec3(0.f);

	renderables.push_back(monkey);

	for (int x = -20; x <= 20; x++) {
		for (int y = -20; y <= 20; y++) {
			RenderObject tri;
			tri.mesh = get_mesh("monkey");
			tri.material = get_material("defaultmesh");
			
			glm::mat4 translation = glm::translate(glm::mat4(1.f), glm::vec3(x, 0, y));
			glm::mat4 scale = glm::scale(glm::mat4(1.f), glm::vec3(0.2f));
			tri.transformMatrix = translation * scale;
			tri.color = glm::vec3(0.f, (float(x)) / 20.f, (float(y)) / 20.f);

			renderables.push_back(tri);
		}
	}
}

void VulkanEngine::init_descriptors() {
	const size_t camSceneBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(CamSceneData));

	std::vector<VkDescriptorPoolSize> sizes = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10}
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 10;
	poolInfo.poolSizeCount = (uint32_t)sizes.size();
	poolInfo.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

	VkDescriptorSetLayoutBinding camSceneBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 1;
	setInfo.pBindings = &camSceneBinding;

	VkDescriptorSetLayoutBinding objectBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutBinding colorBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);

	VkDescriptorSetLayoutBinding storageBindings[] = {objectBinding, colorBinding};

	VkDescriptorSetLayoutCreateInfo set2Info{};
	set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set2Info.bindingCount = 2;
	set2Info.pBindings = storageBindings;

	vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &globalSetLayout);
	vkCreateDescriptorSetLayout(device, &set2Info, nullptr, &storageSetLayout);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		const int MAX_OBJECTS = 10000;

		frames[i].objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		frames[i].colorBuffer = create_buffer(sizeof(GPUColorData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorSetAllocateInfo objectAllocInfo{};
		objectAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objectAllocInfo.descriptorPool = descriptorPool;
		objectAllocInfo.descriptorSetCount = 1;
		objectAllocInfo.pSetLayouts = &storageSetLayout;

		vkAllocateDescriptorSets(device, &objectAllocInfo, &frames[i].storageDescriptor);

		//point to buffers
		VkDescriptorBufferInfo objectInfo{};
		objectInfo.buffer = frames[i].objectBuffer.buffer;
		objectInfo.offset = 0;
		objectInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		VkDescriptorBufferInfo colorInfo{};
		colorInfo.buffer = frames[i].colorBuffer.buffer;
		colorInfo.offset = 0;
		colorInfo.range = sizeof(GPUColorData) * MAX_OBJECTS;

		VkWriteDescriptorSet objectWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frames[i].storageDescriptor, &objectInfo, 0);
		VkWriteDescriptorSet colorWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frames[i].storageDescriptor, &colorInfo, 1);
		VkWriteDescriptorSet writes[] = {objectWrite, colorWrite};

		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
	}

	//combined frame buffer setup
	camSceneBuffer = create_buffer(camSceneBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &globalSetLayout;

	vkAllocateDescriptorSets(device, &allocInfo, &camSceneDescriptor);

	//range is for one instance not the whole buffer (so if buffer holds info for two frames the range should only be for one)
	VkDescriptorBufferInfo camSceneInfo{};
	camSceneInfo.buffer = camSceneBuffer.buffer;
	camSceneInfo.offset = 0;
	camSceneInfo.range = sizeof(CamSceneData);

	VkWriteDescriptorSet camSceneWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, camSceneDescriptor, &camSceneInfo, 0);

	vkUpdateDescriptorSets(device, 1, &camSceneWrite, 0, nullptr);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		deletionQueue.push_function([=]() {
			vmaDestroyBuffer(allocator, frames[i].objectBuffer.buffer, frames[i].objectBuffer.allocation);
			vmaDestroyBuffer(allocator, frames[i].colorBuffer.buffer, frames[i].colorBuffer.allocation);
		});
	}

	deletionQueue.push_function([=]() {
		vmaDestroyBuffer(allocator, camSceneBuffer.buffer, camSceneBuffer.allocation);
		vkDestroyDescriptorSetLayout(device, globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, storageSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	});
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	//read file into a buffer
	size_t fileSize = (size_t)file.tellg();
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule module;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &module));
	*outShaderModule = module;
	return true;
}

void VulkanEngine::load_meshes() {
	Mesh triangleMesh;
	triangleMesh.vertices.resize(3);
	triangleMesh.vertices[0].position = {1.f, 1.f, 0.f};
	triangleMesh.vertices[1].position = {-1.f, 1.f, 0.f};
	triangleMesh.vertices[2].position = {0.f, -1.f, 0.f};

	triangleMesh.vertices[0].color = {1.f, 0.f, 0.f};
	triangleMesh.vertices[1].color = {0.f, 1.f, 0.f};
	triangleMesh.vertices[2].color = {0.f, 0.f, 1.f};

	Mesh monkeyMesh;
	monkeyMesh.load_from_obj("../assets/monkey_flat.obj");

	//puts into buffer
	upload_mesh(triangleMesh);
	upload_mesh(monkeyMesh);

	meshes["monkey"] = monkeyMesh;
	meshes["triangle"] = triangleMesh;
}

void VulkanEngine::upload_mesh(Mesh& mesh) {
	const size_t bufferSize = mesh.vertices.size() * sizeof(Vertex);

	VkBufferCreateInfo stagingInfo{};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = bufferSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &vmaAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

	//copy vertex data to buffer
	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	//allocate vertex buffer
	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &vertexBufferInfo, &vmaAllocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr));

	//copy buffer
	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.size = bufferSize;
		copy.srcOffset = 0;
		copy.dstOffset = 0;
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
	});

	deletionQueue.push_function([=]() {
		vmaDestroyBuffer(allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
	});

	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;
	
	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = memoryUsage;

	AllocatedBuffer buffer;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, nullptr));
	return buffer;
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize) {
	size_t minUboAlignment = gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0) {
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return alignedSize;
}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout pipelineLayout, const std::string& name) {
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = pipelineLayout;
	materials[name] = mat;
	return &materials[name];
}

Material* VulkanEngine::get_material(const std::string& name) {
	auto it = materials.find(name);
	return it == materials.end() ? nullptr : &it->second;
}

Mesh* VulkanEngine::get_mesh(const std::string& name) {
	auto it = meshes.find(name);
	return it == meshes.end() ? nullptr : &it->second;
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject* first, int count) {
	glm::mat4 view = glm::translate(glm::mat4(1.f), cameraPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.f), (float) _windowExtent.width / _windowExtent.height, 0.1f, 200.f);
	glm::mat4 spin = glm::rotate(glm::mat4{1.f}, glm::radians(0.6f), glm::vec3(1, 0, 0));

	//vulkan y up lol
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewProj = projection * view;

	//copy scene data into uniform buffer
	int frameIndex = _frameNumber % FRAME_OVERLAP;
	float framed = (_frameNumber / 120.f);
	sceneParameters.ambientColor = {sin(framed), 0.f, cos(framed), 1.f};

	void* camSceneData;
	vmaMapMemory(allocator, camSceneBuffer.allocation, &camSceneData);
	CamSceneData* pCamScene = (CamSceneData*) camSceneData;
	pCamScene[frameIndex].camera = camData;
	pCamScene[frameIndex].scene = sceneParameters;
	vmaUnmapMemory(allocator, camSceneBuffer.allocation);

	//copy object data into storage buffer
	void* objectData;
	vmaMapMemory(allocator, get_current_frame().objectBuffer.allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*) objectData;
	for (int i = 0; i < count; i++) {
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;
	}
	vmaUnmapMemory(allocator, get_current_frame().objectBuffer.allocation);

	//copy color data into storage buffer
	void* colorData;
	vmaMapMemory(allocator, get_current_frame().colorBuffer.allocation, &colorData);
	GPUColorData* colorSSBO = (GPUColorData*) colorData;
	for (int i = 0; i < count; i++) {
		RenderObject& object = first[i];
		colorSSBO[i].color = glm::vec3(object.color);
	}
	vmaUnmapMemory(allocator, get_current_frame().colorBuffer.allocation);

	Mesh* lastMesh;
	Material* lastMaterial;
	for (int i = 0; i < count; i++) {
		RenderObject& object = first[i];
		//object.transformMatrix *= spin;

		if (object.material != lastMaterial) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;

			uint32_t uniform_offset = pad_uniform_buffer_size(sizeof(CamSceneData)) * frameIndex;
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &camSceneDescriptor, 1, &uniform_offset);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &get_current_frame().storageDescriptor, 0, nullptr);
		}

		MeshPushConstants constants;
		constants.render_matrix = object.transformMatrix;

		vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		if (object.mesh != lastMesh) {
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->vertexBuffer.buffer, &offset);
			lastMesh = object.mesh;
		}

		vkCmdDraw(cmd, object.mesh->vertices.size(), 1, 0, i);
	}
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
	VkCommandBuffer cmd = uploadContext.uploadBuffer;
	VkCommandBufferBeginInfo beginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

	function(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo submitInfo = vkinit::submitInfo(&cmd);

	VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, uploadContext.uploadFence));
	vkWaitForFences(device, 1, &uploadContext.uploadFence, VK_TRUE, 9999999999);
	vkResetFences(device, 1, &uploadContext.uploadFence);

	vkResetCommandPool(device, uploadContext.uploadPool, 0);
}

FrameData& VulkanEngine::get_current_frame() {
	return frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::cleanup() {
	if (_isInitialized) {
		VkFence renderFences[FRAME_OVERLAP];
		for (int i = 0; i < FRAME_OVERLAP; i++) {
			renderFences[i] = frames[i].renderFence;
		}

		//wait on ALL render fences (double buffering is trolling)
		vkWaitForFences(device, FRAME_OVERLAP, renderFences, VK_TRUE, 1000000000);

		deletionQueue.flush();
		vmaDestroyAllocator(allocator);

		vkDestroyDevice(device, nullptr);
		vkDestroySurfaceKHR(instance, surface, nullptr);
		vkb::destroy_debug_utils_messenger(instance, debugMessenger);
		vkDestroyInstance(instance, nullptr);
		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw() {
	FrameData& currentFrame = get_current_frame();

	//wait for previous gpu instructions to finish
	VK_CHECK(vkWaitForFences(device, 1, &currentFrame.renderFence, VK_TRUE, 1000000000));
	VK_CHECK(vkResetFences(device, 1, &currentFrame.renderFence));

	//grab image from swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex));

	//prepare command buffer for commands
	VK_CHECK(vkResetCommandBuffer(currentFrame.commandBuffer, 0));
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(currentFrame.commandBuffer, &cmdBeginInfo));

	//record commands into buffer
	VkClearValue clearValue;
	clearValue.color = {{0.1f, 0.12f, 0.15f, 1.f}};
	
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 0.f;

	std::vector<VkClearValue> clearValues;

	clearValues.push_back(clearValue);
	clearValues.push_back(depthClear);

	VkRenderPassBeginInfo rpBeginInfo = {};
	rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBeginInfo.framebuffer = framebuffers[swapchainImageIndex];
	rpBeginInfo.clearValueCount = clearValues.size();
	rpBeginInfo.pClearValues = clearValues.data();
	rpBeginInfo.renderPass = renderPass;
	rpBeginInfo.renderArea.offset = {0, 0};
	rpBeginInfo.renderArea.extent = _windowExtent;

	vkCmdBeginRenderPass(currentFrame.commandBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_objects(currentFrame.commandBuffer, renderables.data(), renderables.size());

	vkCmdEndRenderPass(currentFrame.commandBuffer);
	VK_CHECK(vkEndCommandBuffer(currentFrame.commandBuffer));

	//submit to queue
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &currentFrame.presentSemaphore;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &currentFrame.renderSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &currentFrame.commandBuffer;

	VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, currentFrame.renderFence));

	//present queue results
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

	_frameNumber++;
}

void VulkanEngine::run() {
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit) {
		const Uint8* keyState;

		// Handle events on queue
		while (SDL_PollEvent(&e) != 0) {
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_QUIT) {
				bQuit = true;
			}

			keyState = SDL_GetKeyboardState(NULL);
		}

		double deltaTime = (SDL_GetTicks() - _lastTime) * (60.f/1000.f);

		glm::vec3 movement = glm::vec3(0.f);

		if (keyState[SDL_SCANCODE_W]) {
			movement.z += 1;
		}

		if (keyState[SDL_SCANCODE_S]) {
			movement.z -= 1;
		}

		if (keyState[SDL_SCANCODE_A]) {
			movement.x += 1;
		}

		if (keyState[SDL_SCANCODE_D]) {
			movement.x -= 1;
		}

		if (keyState[SDL_SCANCODE_LSHIFT]) {
			movement.y += 1;
		}

		if (keyState[SDL_SCANCODE_SPACE]) {
			movement.y -= 1;
		}

		cameraPos += movement == glm::vec3(0.f) ? movement : glm::normalize(movement) * cameraSpeed * (float) deltaTime;

		_lastTime = SDL_GetTicks();

		draw();
	}
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass) {
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = _shaderStages.size();
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	//it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
		device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		cout << "failed to create pipeline" << endl;;
		return VK_NULL_HANDLE;
	} else {
		return newPipeline;
	}
}