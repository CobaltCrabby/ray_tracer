
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include <iostream>
#include <fstream>

#include "VkBootstrap.h"
#include "vk_textures.h"

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
		"raytracer",
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

	load_images();
	load_meshes();

	init_scene();
	init_imgui();

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

	//vulkan is lying to me there is only 1 queue so they are the same
	computeQueue = graphicsQueue;
	computeQueueFamily = graphicsQueueFamily;

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
	surfaceFormat.format = VK_FORMAT_B8G8R8A8_SRGB;

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

	deletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(device, swapchain, nullptr);
	});
}

void VulkanEngine::init_commands() {
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	VkCommandPoolCreateInfo computeCmdPoolInfo = vkinit::commandPoolCreateInfo(computeQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));
		VK_CHECK(vkCreateCommandPool(device, &computeCmdPoolInfo, nullptr, &frames[i].computeCmdPool));

		VkCommandBufferAllocateInfo commandBufferInfo = vkinit::commandBufferAllocateInfo(frames[i].commandPool);
		VkCommandBufferAllocateInfo computeCmdBufferInfo = vkinit::commandBufferAllocateInfo(frames[i].computeCmdPool);
		VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferInfo, &frames[i].commandBuffer));
		VK_CHECK(vkAllocateCommandBuffers(device, &computeCmdBufferInfo, &frames[i].computeCmdBuffer));

		deletionQueue.push_function([=]() {
			vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
			vkDestroyCommandPool(device, frames[i].computeCmdPool, nullptr);
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

	VkSubpassDescription subpass = {};
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
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
		VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frames[i].computeFence));

		deletionQueue.push_function([=]() {
			vkDestroyFence(device, frames[i].renderFence, nullptr);
			vkDestroyFence(device, frames[i].computeFence, nullptr);
		});

		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].renderSemaphore));
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].presentSemaphore));
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].computeSemaphore));

		deletionQueue.push_function([=]() {
			vkDestroySemaphore(device, frames[i].renderSemaphore, nullptr);
			vkDestroySemaphore(device, frames[i].presentSemaphore, nullptr);
			vkDestroySemaphore(device, frames[i].computeSemaphore, nullptr);
		});
	}
}

void VulkanEngine::init_pipelines() {
	//load shader binaries
	std::string bin = std::filesystem::current_path().generic_string() + "/../shaders/bin/";
	VkShaderModule fragment;
	if (!load_shader_module((bin + "raytrace.frag.spv").c_str(), &fragment)) {
		cout << "error loading fragment shader" << endl;
	} else {
		cout << "successfully loaded fragment shader" << endl;
	}

	VkShaderModule vertex;
	if (!load_shader_module((bin + "raytrace.vert.spv").c_str(), &vertex)) {
		cout << "error loading vertex shader" << endl;
	} else {
		cout << "successfully loaded vertex shader" << endl;
	}

	VkShaderModule compute;
	if (!load_shader_module((bin + "raytrace.comp.spv").c_str(), &compute)) {
		cout << "error loading compute shader" << endl;
	} else {
		cout << "successfully loaded compute shader" << endl;
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

	VkPipelineLayoutCreateInfo texturedPipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();

	VkDescriptorSetLayout texturedSetLayouts[] = {singleTextureLayout};

	texturedPipelineLayoutInfo.pSetLayouts = texturedSetLayouts;
	texturedPipelineLayoutInfo.setLayoutCount = 1;

	VkPipelineLayout texturedPipeLayout;

	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	computePipelineLayoutInfo.pSetLayouts = &computeLayout;
	computePipelineLayoutInfo.setLayoutCount = 1;

	VkPushConstantRange pushConsant;
	pushConsant.offset = 0;
	pushConsant.size = sizeof(PushConstants);
	pushConsant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computePipelineLayoutInfo.pushConstantRangeCount = 1;
	computePipelineLayoutInfo.pPushConstantRanges = &pushConsant;

	VK_CHECK(vkCreatePipelineLayout(device, &texturedPipelineLayoutInfo, nullptr, &texturedPipeLayout));
	VK_CHECK(vkCreatePipelineLayout(device, &computePipelineLayoutInfo, nullptr, &computePipeLayout));

	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertex));
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragment));
	builder._pipelineLayout = texturedPipeLayout;

	create_material(builder.build_pipeline(device, renderPass), texturedPipeLayout, "defaultmesh");

	VkComputePipelineCreateInfo computePipelineInfo = vkinit::computePipelineCreateInfo(computePipeLayout);
	computePipelineInfo.stage = vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, compute);
	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computePipeline));

	//can delete after pipeline creation
	vkDestroyShaderModule(device, fragment, nullptr);
	vkDestroyShaderModule(device, vertex, nullptr);
	vkDestroyShaderModule(device, compute, nullptr);

	deletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(device, computePipeLayout, nullptr);
		vkDestroyPipeline(device, computePipeline, nullptr);
	});

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
	RenderObject quad;
	quad.transformMatrix = glm::mat4();
	quad.material = get_material("defaultmesh");
	quad.mesh = get_mesh("quad");
	quad.color = glm::vec3(0.f);

	renderables.push_back(quad);

	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(VK_FILTER_NEAREST);

	VkSampler blockySampler;
	vkCreateSampler(device, &samplerInfo, nullptr, &blockySampler);

	Material* mat = get_material("defaultmesh");

	//allocate the descriptor set for single-texture to use on the material
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &singleTextureLayout;

	vkAllocateDescriptorSets(device, &allocInfo, &mat->textureSet);

	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = blockySampler;
	imageBufferInfo.imageView = textures["compute"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet texture1 = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mat->textureSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(device, 1, &texture1, 0, nullptr);

	//allocate the descriptor set for compute
	VkDescriptorSetAllocateInfo compAllocInfo = {};
	compAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	compAllocInfo.descriptorPool = descriptorPool;
	compAllocInfo.descriptorSetCount = 1;
	compAllocInfo.pSetLayouts = &computeLayout;

	vkAllocateDescriptorSets(device, &compAllocInfo, &computeSet);

	VkDescriptorImageInfo compImageInfo;
	compImageInfo.sampler = blockySampler;
	compImageInfo.imageView = textures["compute"].imageView;
	compImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorBufferInfo sphereBufferInfo;
	sphereBufferInfo.buffer = sphereBuffer.buffer;
	sphereBufferInfo.offset = 0;
	sphereBufferInfo.range = sizeof(Sphere) * MAX_SPHERES;

	VkDescriptorBufferInfo materialBufferInfo;
	materialBufferInfo.buffer = materialBuffer.buffer;
	materialBufferInfo.offset = 0;
	materialBufferInfo.range = sizeof(RayMaterial) * MAX_MATERIALS;

	VkWriteDescriptorSet compTex = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, computeSet, &compImageInfo, 0);
	VkWriteDescriptorSet sphereWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &sphereBufferInfo, 1);
	VkWriteDescriptorSet materialWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &materialBufferInfo, 2);
	
	VkWriteDescriptorSet computeWrites[] = {compTex, sphereWrite, materialWrite};

	vkUpdateDescriptorSets(device, 3, computeWrites, 0, nullptr);

	deletionQueue.push_function([=]() {
		vkDestroySampler(device, blockySampler, nullptr);
	});
}

void VulkanEngine::init_descriptors() {
	std::vector<VkDescriptorPoolSize> sizes = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10}
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 10;
	poolInfo.poolSizeCount = (uint32_t)sizes.size();
	poolInfo.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

	VkDescriptorSetLayoutBinding textureBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 1;
	setInfo.pBindings = &textureBinding;

	vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &singleTextureLayout);

	//compute descriptors
	VkDescriptorSetLayoutBinding computeBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0);
	VkDescriptorSetLayoutBinding sphereBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1);
	VkDescriptorSetLayoutBinding materialBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);

	VkDescriptorSetLayoutBinding computeBindings[] = {computeBinding, sphereBufferBinding, materialBufferBinding};

	VkDescriptorSetLayoutCreateInfo computeSetInfo{};
	computeSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	computeSetInfo.bindingCount = 3;
	computeSetInfo.pBindings = computeBindings;

	vkCreateDescriptorSetLayout(device, &computeSetInfo, nullptr, &computeLayout);

	deletionQueue.push_function([=]() {
		vkDestroyDescriptorSetLayout(device, singleTextureLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, computeLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	});
}

void VulkanEngine::init_imgui() {
	//oversized but copied from an exmaple
	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = std::size(pool_sizes);
	poolInfo.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiPool));

	//initialize library
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = instance;
	init_info.PhysicalDevice = physicalDevice;
	init_info.Device = device;
	init_info.Queue = graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.RenderPass = renderPass;
	init_info.QueueFamily = graphicsQueueFamily;
	init_info.Subpass = 0;


	ImGui_ImplVulkan_Init(&init_info);	

	//execute a gpu command to upload imgui font textures
	immediate_submit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture();
	});

	//clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontsTexture();

	deletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(device, imguiPool, nullptr);
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
	Mesh quad;
	quad.vertices.resize(6);
	quad.vertices[0].position = {-1.f, -1.f, 0.f};
	quad.vertices[1].position = {-1.f, 1.f, 0.f};
	quad.vertices[2].position = {1.f, -1.f, 0.f};
	quad.vertices[0].uv = {0.f, 0.f};
	quad.vertices[1].uv = {0.f, 1.f};
	quad.vertices[2].uv = {1.f, 0.f};

	quad.vertices[3].position = {-1.f, 1.f, 0.f};
	quad.vertices[4].position = {1.f, -1.f, 0.f};
	quad.vertices[5].position = {1.f, 1.f, 0.f};
	quad.vertices[3].uv = {0.f, 1.f};
	quad.vertices[4].uv = {1.f, 0.f};
	quad.vertices[5].uv = {1.f, 1.f};

	upload_mesh(quad);

	meshes["quad"] = quad;

	//spheres
	spheres.push_back({glm::vec3(3.f, 0.2f, 5.5f), 1.f, 0});
	spheres.push_back({glm::vec3(0.5f, -0.3f, 6.f), 1.5f, 1});
	spheres.push_back({glm::vec3(0.f, 31.f, 6.f), 30.f, 2});
	spheres.push_back({glm::vec3(-3.f, -0.8f, 6.f), 2.f, 3});

	copy_buffer(sizeof(Sphere) * MAX_SPHERES, sphereBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) spheres.data());

	//materials
	RayMaterial mat0;
	mat0.albedo = glm::vec3(1.f, 0.f, 0.f);
	mat0.emissionColor = glm::vec3(0.f);
	mat0.emissionStrength = 0.f;

	RayMaterial mat1;
	mat1.albedo = glm::vec3(0.f);
	mat1.emissionColor = glm::vec3(1.f, 0.f, 0.f);
	mat1.emissionStrength = 2.f;

	RayMaterial mat2;
	mat2.albedo = glm::vec3(1.f);
	mat2.emissionColor = glm::vec3(0.f);
	mat2.emissionStrength = 0.f;

	RayMaterial mat3;
	mat3.albedo = glm::vec3(96/255.f, 73/255.f, 245/255.f);
	mat3.emissionColor = glm::vec3(0.f);
	mat3.emissionStrength = 0.f;

	RayMaterial mat4;
	mat4.albedo = glm::vec3(0.f, 0.4f, 0.1f);
	mat4.emissionColor = glm::vec3(0.f, 0.4f, 0.1f);
	mat4.emissionStrength = 0.f;

	rayMaterials.push_back(mat1);
	rayMaterials.push_back(mat2);
	rayMaterials.push_back(mat3);
	rayMaterials.push_back(mat4);

	copy_buffer(sizeof(RayMaterial) * MAX_MATERIALS, materialBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) rayMaterials.data());
}

void VulkanEngine::load_images() {
	Texture computeResult;

	vkutil::create_empty_image(*this, computeResult.image, _windowExtent);

	VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, computeResult.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &computeResult.imageView));

	textures["compute"] = computeResult;

	deletionQueue.push_function([=]() {
		vkDestroyImageView(device, computeResult.imageView, nullptr);
	});
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

void VulkanEngine::copy_buffer(size_t bufferSize, AllocatedBuffer& buffer, VkBufferUsageFlags flags, void* bufferData) {
	VkBufferCreateInfo stagingInfo{};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = bufferSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &vmaAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

	//copy data to staging buffer
	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, bufferData, bufferSize);
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	//allocate sphere buffer
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bufferSize;
	bufferInfo.usage = flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &buffer.buffer, &buffer.allocation, nullptr));

	//copy buffer
	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.size = bufferSize;
		copy.srcOffset = 0;
		copy.dstOffset = 0;
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, buffer.buffer, 1, &copy);
	});

	deletionQueue.push_function([=]() {
		vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
	});

	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);
}

void VulkanEngine::update_buffer(size_t bufferSize, AllocatedBuffer& buffer, void* bufferData) {
	VkBufferCreateInfo stagingInfo{};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = bufferSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &vmaAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

	//copy data to staging buffer
	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, bufferData, bufferSize);
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	//copy buffer
	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.size = bufferSize;
		copy.srcOffset = 0;
		copy.dstOffset = 0;
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, buffer.buffer, 1, &copy);
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

void VulkanEngine::dispatch_compute(VkQueue queue, VkCommandBuffer cmd) {
	VkCommandBufferBeginInfo beginInfo = vkinit::commandBufferBeginInfo();
	vkBeginCommandBuffer(cmd, &beginInfo);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeLayout, 0, 1, &computeSet, 0, nullptr);

	PushConstants constants;
	cameraInfo.aspectRatio = _windowExtent.width / (float) _windowExtent.height;

	float thetaX = glm::radians(cameraAngles[0]);
	float thetaY = glm::radians(cameraAngles[1]);
	float thetaZ = glm::radians(cameraAngles[2]);
	glm::mat3 rotX = glm::mat3(
		glm::vec3(1, 0, 0),
		glm::vec3(0, cos(thetaX), -sin(thetaX)),
		glm::vec3(0, sin(thetaX), cos(thetaX))	
	);

	glm::mat3 rotY = glm::mat3(
		glm::vec3(cos(thetaY), 0, sin(thetaY)),
		glm::vec3(0, 1, 0),
		glm::vec3(-sin(thetaY), 0, cos(thetaY))
	);

	glm::mat3 rotZ = glm::mat3(
		glm::vec3(cos(thetaZ), -sin(thetaZ), 0),
		glm::vec3(sin(thetaZ), cos(thetaZ), 0),
		glm::vec3(0, 0, 1)
	);
	cameraInfo.cameraRotation = rotX * rotY * rotZ;

	glm::vec3 sun = normalize(glm::vec3(2.f, 0.8f, -3.f));

	rayTracerParams.sphereCount = spheres.size();

	constants.lightDir = sun;
	constants.camInfo = cameraInfo;
	constants.environment = environment;
	constants.rayTraceParams = rayTracerParams;
	constants.frameCount = _frameNumber;

	vkCmdPushConstants(cmd, computePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &constants);
	
	vkCmdDispatch(cmd, ceil(_windowExtent.width / 8.0), ceil(_windowExtent.height / 8.0), 1);
	vkEndCommandBuffer(cmd);
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject* first, int count) {
	//copy scene data into uniform buffer
	RenderObject& object = *first;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &object.material->textureSet, 0, nullptr);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->vertexBuffer.buffer, &offset);
	vkCmdDraw(cmd, object.mesh->vertices.size(), 1, 0, 0);
}

void VulkanEngine::imgui_draw() {
	ImGui::Begin("raytracer... :mydog:");
	ImVec2 windowSize = {400, 400};
	ImGui::SetWindowSize(windowSize);
	
	if (ImGui::CollapsingHeader("Render Stats")) {
		ImGui::Text("drawtime: %.3fms", renderStats.drawTime);
		ImGui::Text("frametime: %.3fms", renderStats.frameTime);
		ImGui::Text("fps: %.1f", 1.f / (renderStats.frameTime / 1000.f));
	}

	if (ImGui::CollapsingHeader("Ray Tracer Info")) {
		ImGui::Checkbox("Progressive Rendering", &rayTracerParams.progressive);
		ImGui::DragInt("Rays Per Pixel", (int*) &rayTracerParams.raysPerPixel, 1.f, 0, 100);
		ImGui::DragInt("Bounce Limit", (int*) &rayTracerParams.bounceLimit, 1.f, 0, 100);
	}

	if (ImGui::CollapsingHeader("Camera Info")) {
		ImGui::DragFloat("Fov", &cameraInfo.fov, 1.f, 30.f, 120.f, "%.1f", 0);
		ImGui::DragFloat3("Camera Rotation (euler angles)", cameraAngles);
		ImGui::DragFloat3("Camera Position", (float*) &cameraInfo.pos, 0.1f);
	}

	if (ImGui::CollapsingHeader("Environment")) {
		ImGui::ColorEdit3("Horizon Color", (float*) &environment.horizonColor);
		ImGui::ColorEdit3("Zenith Color", (float*) &environment.zenithColor);
		ImGui::ColorEdit3("Ground Color", (float*) &environment.groundColor);
		ImGui::DragFloat("Sun Focus", &environment.sunFocus, 0.1f, 0.f, 100.f);
		ImGui::DragFloat("Sun Intensity", &environment.sunIntensity, 0.1f, 0.f, 100.f);
	}

	if (ImGui::CollapsingHeader("Materials")) {
		ImGui::Indent(16.f);

		ImGui::Unindent(4.f);
		if (ImGui::Button("Add Material") && rayMaterials.size() < MAX_MATERIALS) {
			rayMaterials.push_back({glm::vec3(0.f), glm::vec3(0.f), 0.f});
		}

		if (ImGui::Button("Update Buffer")) {
			update_buffer(sizeof(RayMaterial) * MAX_MATERIALS, materialBuffer, rayMaterials.data());
		}
		ImGui::Indent(4.f);

		for (int i = 0; i < rayMaterials.size(); i++) {
			if (ImGui::CollapsingHeader(("Material " + to_string(i)).c_str())) {
				ImGui::Indent(16.f);
				ImGui::ColorEdit3("Albedo", (float*) &rayMaterials[i].albedo);
				ImGui::ColorEdit3("Emission Color", (float*) &rayMaterials[i].emissionColor);
				ImGui::DragFloat("Emission Strength", (float*) &rayMaterials[i].emissionStrength, 0.1f, 0.f, 100.f);
				ImGui::Unindent(16.f);
			}
		}
		ImGui::Unindent(16.f);
	}

	if (ImGui::CollapsingHeader("Spheres")) {
		ImGui::Indent(16.f);
		ImGui::Unindent(4.f);

		if (ImGui::Button("Add Sphere") && spheres.size() < MAX_SPHERES) {
			spheres.push_back({glm::vec3(0.f), 1.f, 0});
		}

		if (ImGui::Button("Update Buffer")) {
			update_buffer(sizeof(Sphere) * MAX_SPHERES, sphereBuffer, spheres.data());
		}

		ImGui::Indent(4.f);

		for (int i = 0; i < spheres.size(); i++) {
			if (ImGui::CollapsingHeader(("Sphere " + to_string(i)).c_str())) {
				ImGui::Indent(16.f);
				ImGui::DragFloat3("Position", (float*) &spheres[i].position, 0.1f);
				ImGui::DragFloat("Radius", &spheres[i].radius, 0.1f, 0.f, 100.f);
				ImGui::DragInt("Material Index", (int*) &spheres[i].materialIndex, 0.1f, 0, rayMaterials.size() - 1);
				ImGui::Unindent(16.f);
			}
		}

		ImGui::Unindent(16.f);
	}

	ImGui::End();
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
	ImGui::Render();

	FrameData& currentFrame = get_current_frame();

	//wait for previous gpu instructions to finish
	VK_CHECK(vkWaitForFences(device, 1, &currentFrame.renderFence, VK_TRUE, 1000000000));
	VK_CHECK(vkResetFences(device, 1, &currentFrame.renderFence));

	//grab image from swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex));

	//prepare command buffer for commands
	VK_CHECK(vkResetCommandBuffer(currentFrame.commandBuffer, 0));
	VK_CHECK(vkResetCommandBuffer(currentFrame.commandBuffer, 0));
	
	auto start = std::chrono::system_clock::now();

	dispatch_compute(computeQueue, currentFrame.computeCmdBuffer);

	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(currentFrame.commandBuffer, &cmdBeginInfo));

	//image barrier for compute result
	VkImageMemoryBarrier barrier;
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.image = textures["compute"].image.image;
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.pNext = nullptr;
	vkCmdPipelineBarrier(currentFrame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

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
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), currentFrame.commandBuffer);

	vkCmdEndRenderPass(currentFrame.commandBuffer);
	VK_CHECK(vkEndCommandBuffer(currentFrame.commandBuffer));

	vkWaitForFences(device, 1, &currentFrame.computeFence, VK_TRUE, 9999999999);
	vkResetFences(device, 1, &currentFrame.computeFence);

	VkSubmitInfo computeSubmitInfo = vkinit::submitInfo(&currentFrame.computeCmdBuffer);
	VK_CHECK(vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, currentFrame.computeFence));

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

	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	renderStats.drawTime = elapsed.count() / 1000.f;

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

	_frameNumber = rayTracerParams.progressive ? ++_frameNumber : 0;
}

void VulkanEngine::run() {
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit) {
		const Uint8* keyState;

		auto start = std::chrono::system_clock::now();

		// Handle events on queue
		while (SDL_PollEvent(&e) != 0) {
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_QUIT) {
				bQuit = true;
			}

			keyState = SDL_GetKeyboardState(NULL);
			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		glm::vec4 my_color;

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);
		ImGui::NewFrame();
		imgui_draw();

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

		auto end = std::chrono::system_clock::now();    
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		renderStats.frameTime = elapsed.count() / 1000.f;
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