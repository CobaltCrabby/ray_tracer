
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

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
	init_image();
	init_imgui();

	generate_quad();
	prepare_storage_buffers();
	update_descriptors();

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

	drawCmdBuffers.resize(swapchainImages.size());

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
	VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

	VkCommandPoolCreateInfo uploadPoolInfo = vkinit::commandPoolCreateInfo(graphicsQueueFamily);
	VK_CHECK(vkCreateCommandPool(device, &uploadPoolInfo, nullptr, &uploadContext.uploadPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(uploadContext.uploadPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &uploadContext.uploadBuffer));

	VkCommandBufferAllocateInfo drawCmdAllocInfo = vkinit::commandBufferAllocateInfo(commandPool, drawCmdBuffers.size());
	VK_CHECK(vkAllocateCommandBuffers(device, &drawCmdAllocInfo, drawCmdBuffers.data()));

	VkCommandBufferAllocateInfo computeCmdAllocInfo = vkinit::commandBufferAllocateInfo(commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(device, &computeCmdAllocInfo, &computeCmdBuffer));

	deletionQueue.push_function([=](){
		vkDestroyCommandPool(device, commandPool, nullptr);
		vkDestroyCommandPool(device, uploadContext.uploadPool, nullptr);
	});
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

	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &renderFence));
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &computeFence));

	deletionQueue.push_function([=]() {
		vkDestroyFence(device, renderFence, nullptr);
		vkDestroyFence(device, computeFence, nullptr);
	});

	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderSemaphore));
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &presentSemaphore));
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &computeSemaphore));
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &graphicsSemaphore));

	// Signal the semaphore
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &graphicsSemaphore;
	VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(graphicsQueue));

	deletionQueue.push_function([=]() {
		vkDestroySemaphore(device, renderSemaphore, nullptr);
		vkDestroySemaphore(device, presentSemaphore, nullptr);
		vkDestroySemaphore(device, computeSemaphore, nullptr);
		vkDestroySemaphore(device, graphicsSemaphore, nullptr);
	});
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

	VkPipelineLayoutCreateInfo graphicsPipeLayoutInfo = vkinit::pipelineLayoutCreateInfo();

	graphicsPipeLayoutInfo.pSetLayouts = &graphicsLayout;
	graphicsPipeLayoutInfo.setLayoutCount = 1;

	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	computePipelineLayoutInfo.pSetLayouts = &computeLayout;
	computePipelineLayoutInfo.setLayoutCount = 1;

	VkPushConstantRange pushConsant;
	pushConsant.offset = 0;
	pushConsant.size = sizeof(PushConstants);
	pushConsant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computePipelineLayoutInfo.pushConstantRangeCount = 1;
	computePipelineLayoutInfo.pPushConstantRanges = &pushConsant;

	VK_CHECK(vkCreatePipelineLayout(device, &graphicsPipeLayoutInfo, nullptr, &graphicsPipelineLayout));
	VK_CHECK(vkCreatePipelineLayout(device, &computePipelineLayoutInfo, nullptr, &computePipeLayout));

	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertex));
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragment));
	builder._pipelineLayout = graphicsPipelineLayout;

	graphicsPipeline = builder.build_pipeline(device, renderPass);

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
		vkDestroyPipelineLayout(device, graphicsPipelineLayout, nullptr);
		vkDestroyPipeline(device, graphicsPipeline, nullptr);
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
	
	//graphics descriptor
	VkDescriptorSetLayoutBinding textureBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 1;
	setInfo.pBindings = &textureBinding;

	vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &graphicsLayout);

	//compute descriptors
	VkDescriptorSetLayoutBinding computeBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0);
	VkDescriptorSetLayoutBinding textureBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1);
	VkDescriptorSetLayoutBinding sphereBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
	VkDescriptorSetLayoutBinding materialBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3);
	VkDescriptorSetLayoutBinding triPointBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 4);
	VkDescriptorSetLayoutBinding triangleBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 5);
	VkDescriptorSetLayoutBinding objectBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 6);

	textureBufferBinding.descriptorCount = 2;

	VkDescriptorSetLayoutBinding computeBindings[] = {computeBinding, sphereBufferBinding, materialBufferBinding, textureBufferBinding, triPointBufferBinding, triangleBufferBinding, objectBufferBinding};

	VkDescriptorSetLayoutCreateInfo computeSetInfo{};
	computeSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	computeSetInfo.bindingCount = 7;
	computeSetInfo.pBindings = computeBindings;

	vkCreateDescriptorSetLayout(device, &computeSetInfo, nullptr, &computeLayout);

	deletionQueue.push_function([=]() {
		vkDestroyDescriptorSetLayout(device, graphicsLayout, nullptr);
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

void VulkanEngine::update_descriptors() {
	VkSampler sampler;
	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(VK_FILTER_NEAREST);
	vkCreateSampler(device, &samplerInfo, nullptr, &sampler);

	//allocate the descriptor set for single-texture
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &graphicsLayout;

	vkAllocateDescriptorSets(device, &allocInfo, &graphicsSet);

	//write to the descriptor set
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = sampler;
	imageBufferInfo.imageView = computeImage.imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet texture1 = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, graphicsSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(device, 1, &texture1, 0, nullptr);

	//allocate the descriptor set for compute
	VkDescriptorSetAllocateInfo compAllocInfo = {};
	compAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	compAllocInfo.descriptorPool = descriptorPool;
	compAllocInfo.descriptorSetCount = 1;
	compAllocInfo.pSetLayouts = &computeLayout;

	vkAllocateDescriptorSets(device, &compAllocInfo, &computeSet);

	VkDescriptorImageInfo compImageInfo;
	compImageInfo.sampler = sampler;
	compImageInfo.imageView = computeImage.imageView;
	compImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorBufferInfo sphereBufferInfo;
	sphereBufferInfo.buffer = sphereBuffer.buffer;
	sphereBufferInfo.offset = 0;
	sphereBufferInfo.range = sizeof(Sphere) * MAX_SPHERES;

	VkDescriptorBufferInfo materialBufferInfo;
	materialBufferInfo.buffer = materialBuffer.buffer;
	materialBufferInfo.offset = 0;
	materialBufferInfo.range = sizeof(RayMaterial) * MAX_MATERIALS;

	VkDescriptorImageInfo textureImageInfos[2];
	for (int i = 0; i < 2; i++) {
		textureImageInfos[i].sampler = sampler;
		textureImageInfos[i].imageView = textures[i].imageView;
		textureImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}

	VkDescriptorBufferInfo triPointBufferInfo;
	triPointBufferInfo.buffer = triPointBuffer.buffer;
	triPointBufferInfo.offset = 0;
	triPointBufferInfo.range = sizeof(TrianglePoint) * triPoints.size();

	VkDescriptorBufferInfo triangleBufferInfo;
	triangleBufferInfo.buffer = triangleBuffer.buffer;
	triangleBufferInfo.offset = 0;
	triangleBufferInfo.range = sizeof(Triangle) * triangles.size();

	VkDescriptorBufferInfo objectBufferInfo;
	objectBufferInfo.buffer = objectBuffer.buffer;
	objectBufferInfo.offset = 0;
	objectBufferInfo.range = sizeof(RenderObject) * objects.size();

	VkWriteDescriptorSet compTex = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, computeSet, &compImageInfo, 0);
	VkWriteDescriptorSet textureWrite = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, computeSet, textureImageInfos, 1);
	VkWriteDescriptorSet sphereWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &sphereBufferInfo, 2);
	VkWriteDescriptorSet materialWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &materialBufferInfo, 3);
	VkWriteDescriptorSet triPointWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &triPointBufferInfo, 4);
	VkWriteDescriptorSet triangleWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &triangleBufferInfo, 5);
	VkWriteDescriptorSet objectWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &objectBufferInfo, 6);

	textureWrite.descriptorCount = 2;
	
	VkWriteDescriptorSet computeWrites[] = {compTex, textureWrite, sphereWrite, materialWrite, triPointWrite, triangleWrite, objectWrite};

	vkUpdateDescriptorSets(device, 7, computeWrites, 0, nullptr);

	deletionQueue.push_function([=]() {
		vkDestroySampler(device, sampler, nullptr);
	});
}

void VulkanEngine::prepare_storage_buffers() {
	//spheres
	spheres.resize(MAX_SPHERES);
	//spheres.re
	copy_buffer(sizeof(Sphere) * MAX_SPHERES, sphereBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) spheres.data());

	//materials
	RayMaterial object;
	object.albedoIndex = 0;
	object.metalnessIndex = 1;

	RayMaterial white;

	RayMaterial red;
	red.albedo = glm::vec3(1.f, 0.f, 0.f);

	RayMaterial green;
	green.albedo = glm::vec3(0.f, 1.f, 0.f);

	RayMaterial light;
	light.emissionColor = glm::vec3(1.f);
	light.emissionStrength = 1.f;

	rayMaterials.push_back(object);
	rayMaterials.push_back(white);
	rayMaterials.push_back(red);
	rayMaterials.push_back(green);
	rayMaterials.push_back(light);

	copy_buffer(sizeof(RayMaterial) * MAX_MATERIALS, materialBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) rayMaterials.data());

	//ccw
	ImGuiObject slosh;
	slosh.name = "slosher";
	slosh.position = glm::vec3(-0.4f, 0.35f, 0.f);
	slosh.rotation = glm::vec3(0.f);
	read_obj("../assets/rb.obj", triPoints.size(), triangles.size(), slosh, 0);

	ImGuiObject plane;
	plane.name = "bottom";
	plane.position = glm::vec3(0.f, 0.5f, 0.f);
	plane.rotation = glm::vec3(0.f);
	read_obj("../assets/plane.obj", triPoints.size(), triangles.size(), plane, 1);

	plane.name = "left";
	plane.position = glm::vec3(-1.f, -0.5f, 0.f);
	plane.rotation = glm::vec3(90.f, 0.f, 90.f);
	read_obj("../assets/plane.obj", triPoints.size(), triangles.size(), plane, 2);

	plane.name = "right";
	plane.position = glm::vec3(1.f, -0.5f, 0.f);
	plane.rotation = glm::vec3(90.f, 0.f, -90.f);
	read_obj("../assets/plane.obj", triPoints.size(), triangles.size(), plane, 3);

	plane.name = "top";
	plane.position = glm::vec3(0.f, -1.5f, 0.f);
	plane.rotation = glm::vec3(180.f, 0.f, 0.f);
	read_obj("../assets/plane.obj", triPoints.size(), triangles.size(), plane, 4);

	plane.name = "back";
	plane.position = glm::vec3(0.f, -0.5f, 1.f);
	plane.rotation = glm::vec3(90.f, 0.f, 0.f);
	read_obj("../assets/plane.obj", triPoints.size(), triangles.size(), plane, 1);

	plane.name = "front";
	plane.position = glm::vec3(0.f, -0.5f, -1.f);
	plane.rotation = glm::vec3(-90.f, 0.f, 0.f);
	read_obj("../assets/plane.obj", triPoints.size(), triangles.size(), plane, 1);

	copy_buffer(sizeof(TrianglePoint) * triPoints.size(), triPointBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) triPoints.data());
	copy_buffer(sizeof(Triangle) * triangles.size(), triangleBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) triangles.data());
	copy_buffer(sizeof(RenderObject) * objects.size(), objectBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) objects.data());
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

void VulkanEngine::generate_quad() {
	std::vector<Vertex> vertices = {
		{{1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
		{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
		{{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}}
	};

	std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
	
	copy_buffer(sizeof(Vertex) * 4, vertexBuffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
	copy_buffer(sizeof(uint32_t) * 6, indexBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data());
}

void VulkanEngine::read_obj(std::string filePath, int pointOffset, int triOffset, ImGuiObject imGuiObj, int material) {
	std::ifstream fileStream;
	fileStream.open(filePath);

	std::string fileLine;
	bool vertex = false;
	std::vector<glm::vec2> uvs;
	std::vector<glm::vec3> normals;
	glm::vec3 bounds[2] = {glm::vec3(99999.f), glm::vec3(-99999.f)};

	if (!fileStream.is_open()) return;
	while (fileStream) {
		std::string prefix;

		std::getline(fileStream, fileLine);
		if (fileLine.size() <= 1) break;
		for (int i = 0; i < 2; i++) {
			if (fileLine.at(i) != ' ') {
				prefix += fileLine.at(i);
			} else {
				break;
			} 
		}

		int index = 2;
		if (prefix == "v") { //vertices
			glm::vec3 position;
			for (int i = 0; i < 3; i++) {
				int size = fileLine.at(index) == '-' ? 9 : 8;
				position[i] = stof(fileLine.substr(index, size));
				index += size + 1;

				//for bounding box
				bounds[0][i] = min(bounds[0][i], position[i]);
				bounds[1][i] = max(bounds[1][i], position[i]);
			}
			triPoints.push_back({position});
		} else if (prefix == "vt") { //uv
			index++;
			glm::vec2 uv;
			for (int i = 0; i < 2; i++) {
				int size = 8;
				uv[i] = stof(fileLine.substr(index, size));
				index += size + 1;
			}
			uvs.push_back(uv);
		} else if (prefix == "vn") { //normal
			index++;
			glm::vec3 normal;
			for (int i = 0; i < 3; i++) {
				int size = fileLine.at(index) == '-' ? 7 : 6;
				normal[i] = stof(fileLine.substr(index, size));
				index += size + 1;
			}
			normals.push_back(normal);
		} else if (prefix == "f") { //triangles
			index = 0;
			glm::uvec3 vertexIndex;
			for (int i = 0; i < 3; i++) {
				int space = fileLine.find(' ', index);
				int nextSpace = fileLine.find(' ', space + 1);
				std::string vertex = fileLine.substr(space + 1, nextSpace - space - (i == 2 ? 0 : 1));

				int firstSlash = vertex.find('/');
				vertexIndex[i] = stoi(vertex.substr(0, firstSlash)) + pointOffset - 1;
				int secondSlash = vertex.find('/', firstSlash + 1);
				triPoints[vertexIndex[i]].uv = uvs.at(stoi(vertex.substr(firstSlash + 1, secondSlash - firstSlash - 1)) - 1);
				triPoints[vertexIndex[i]].normal = normals.at(stoi(vertex.substr(secondSlash + 1, vertex.size() - secondSlash - 1)) - 1);

				index = nextSpace;
			}

			Triangle tri;
			tri.indices = vertexIndex;
			triangles.push_back(tri);
 		}
	}

	RenderObject object;
	object.materialIndex = material;
	object.transformMatrix = glm::translate(imGuiObj.position) * 
		glm::rotate(glm::radians(imGuiObj.rotation.x), glm::vec3(1.f, 0.f, 0.f)) * 
		glm::rotate(glm::radians(imGuiObj.rotation.y), glm::vec3(0.f, 1.f, 0.f)) * 
		glm::rotate(glm::radians(imGuiObj.rotation.z), glm::vec3(0.f, 0.f, 1.f));
	object.triangleCount = triangles.size() - triOffset;
	object.triangleStart = triOffset;
	object.boundingBox.bounds[0] = glm::vec4(bounds[0], 0.f);
	object.boundingBox.bounds[1] = glm::vec4(bounds[1], 0.f);
	object.smoothShade = false;
	objects.push_back(object);

	imGuiObjects.push_back(imGuiObj);
	cout << "Object at " << filePath << ": " << object.triangleCount << " tris, " << uvs.size() << " verts" << endl;
}

void VulkanEngine::init_image() {
	textures.resize(2);

	vkutil::create_empty_image(*this, computeImage.image, _windowExtent);
	vkutil::load_image_from_file(*this, "../assets/rb_alb.png", textures[0].image);
	vkutil::load_image_from_file(*this, "../assets/rb_mtl.png", textures[1].image);

	VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, computeImage.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &computeImage.imageView));

	VkImageViewCreateInfo viewInfo2 = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textures[0].image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(device, &viewInfo2, nullptr, &textures[0].imageView));

	VkImageViewCreateInfo viewInfo3 = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textures[1].image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(device, &viewInfo3, nullptr, &textures[1].imageView));

	deletionQueue.push_function([=]() {
		vkDestroyImageView(device, computeImage.imageView, nullptr);
		vkDestroyImageView(device, textures[0].imageView, nullptr);
		vkDestroyImageView(device, textures[1].imageView, nullptr);
	});
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
		ImGui::DragInt("Rays Per Pixel", (int*) &rayTracerParams.raysPerPixel, 1.f, 0, 1000);
		ImGui::DragInt("Bounce Limit", (int*) &rayTracerParams.bounceLimit, 1.f, 0, 100);
	}

	if (ImGui::CollapsingHeader("Camera Info")) {
		ImGui::DragFloat("Fov", &cameraInfo.fov, 1.f, 30.f, 120.f, "%.1f", 0);
		ImGui::DragFloat3("Camera Rotation (euler angles)", cameraAngles);
		ImGui::DragFloat3("Camera Position", (float*) &cameraInfo.pos, 0.1f);
	}

	if (ImGui::CollapsingHeader("Environment")) {
		ImGui::DragFloat("Environment Lighting On", &environment.lightDir.w, 1.f, 0.f, 1.f); //bit magic idk
		ImGui::ColorEdit3("Horizon Color", (float*) &environment.horizonColor);
		ImGui::ColorEdit3("Zenith Color", (float*) &environment.zenithColor);
		ImGui::ColorEdit3("Ground Color", (float*) &environment.groundColor);
		ImGui::DragFloat3("Sun Direction", (float*) &environment.lightDir, 0.01f, 0.f, 1.f);
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
				ImGui::DragFloat("Reflectance", &rayMaterials[i].reflectance, 0.05f, 0.f, 1.f);
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

	if (ImGui::CollapsingHeader("Models")) {
		ImGui::Indent(16.f);

		ImGui::Unindent(4.f);
		if (ImGui::Button("Update Buffer")) {
			for (int i = 0; i < imGuiObjects.size(); i++) {
				ImGuiObject object = imGuiObjects[i];
				objects[i].transformMatrix = glm::translate(object.position) * 
					glm::rotate(glm::radians(object.rotation.x), glm::vec3(1.f, 0.f, 0.f)) * 
					glm::rotate(glm::radians(object.rotation.y), glm::vec3(0.f, 1.f, 0.f)) * 
					glm::rotate(glm::radians(object.rotation.z), glm::vec3(0.f, 0.f, 1.f));
			}
			update_buffer(sizeof(RenderObject) * objects.size(), objectBuffer, objects.data());
		}
		ImGui::Indent(4.f);

		for (int i = 0; i < imGuiObjects.size(); i++) {
			if (ImGui::CollapsingHeader(imGuiObjects[i].name.c_str())) {
				ImGui::Indent(16.f);
				ImGui::DragFloat3("Position", (float*) &imGuiObjects[i].position, 0.1f);
				ImGui::DragFloat3("Rotation", (float*) &imGuiObjects[i].rotation, 1.f);
				ImGui::Checkbox("Smooth Shading", (bool*) &objects[i].smoothShade);
				ImGui::Unindent(16.f);
			}
		}
		ImGui::Unindent(16.f);
	}

	ImGui::End();
}

void VulkanEngine::run_compute() {
	VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	VkCommandBufferBeginInfo computeCmdInfo = vkinit::commandBufferBeginInfo();
	VK_CHECK(vkBeginCommandBuffer(computeCmdBuffer, &computeCmdInfo));

	vkCmdBindPipeline(computeCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
	vkCmdBindDescriptorSets(computeCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeLayout, 0, 1, &computeSet, 0, nullptr);

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

	rayTracerParams.sphereCount = spheres.size();
	rayTracerParams.objectCount = objects.size();

	constants.camInfo = cameraInfo;
	constants.environment = environment;
	constants.rayTraceParams = rayTracerParams;
	constants.frameCount = _frameNumber;

	vkCmdPushConstants(computeCmdBuffer, computePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &constants);

	vkCmdDispatch(computeCmdBuffer, ceil(_windowExtent.width / 8.f), ceil(_windowExtent.height / 8.f), 1);

	vkEndCommandBuffer(computeCmdBuffer);

	VkSubmitInfo computeSubmit = vkinit::submitInfo(&computeCmdBuffer);
	computeSubmit.waitSemaphoreCount = 1;
	computeSubmit.pWaitSemaphores = &graphicsSemaphore;
	computeSubmit.pWaitDstStageMask = &waitStageMask;
	computeSubmit.signalSemaphoreCount = 1;
	computeSubmit.pSignalSemaphores = &computeSemaphore;
	vkQueueSubmit(computeQueue, 1, &computeSubmit, VK_NULL_HANDLE);
}

void VulkanEngine::run_graphics(uint ind) {
	VkCommandBufferAllocateInfo cmdBufferAlloc = vkinit::commandBufferAllocateInfo(commandPool);
	VkCommandBufferBeginInfo cmdBufferInfo = vkinit::commandBufferBeginInfo();

	VkClearColorValue clearColor = {0.f, 0.f, 0.f};
	VkClearValue clearValue;
	clearValue.color = clearColor;

	VkRenderPassBeginInfo rpBeginInfo = {};
	rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBeginInfo.clearValueCount = 1;
	rpBeginInfo.pClearValues = &clearValue;
	rpBeginInfo.renderPass = renderPass;
	rpBeginInfo.renderArea.offset = {0, 0};
	rpBeginInfo.renderArea.extent = _windowExtent;

	VkCommandBuffer currentCmdBuffer = drawCmdBuffers[ind];

	rpBeginInfo.framebuffer = framebuffers[ind];
	VK_CHECK(vkBeginCommandBuffer(currentCmdBuffer, &cmdBufferInfo));

	VkImageMemoryBarrier imageMemoryBarrier{};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageMemoryBarrier.image = computeImage.image.image;
	imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	vkCmdPipelineBarrier(currentCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0 , nullptr, 1, &imageMemoryBarrier);
	vkCmdBeginRenderPass(currentCmdBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(currentCmdBuffer, 0, 1, &vertexBuffer.buffer, &offset);
	vkCmdBindIndexBuffer(currentCmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(currentCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &graphicsSet, 0, nullptr);
	vkCmdBindPipeline(currentCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
	vkCmdDrawIndexed(currentCmdBuffer, 6, 1, 0, 0, 0);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), currentCmdBuffer);

	vkCmdEndRenderPass(currentCmdBuffer);
	vkEndCommandBuffer(currentCmdBuffer);

	VkPipelineStageFlags graphicsWaitStageMasks[] = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore graphicsWaitSemaphores[] = {computeSemaphore, presentSemaphore};
	VkSemaphore graphicsSignalSemaphores[] = {graphicsSemaphore, renderSemaphore};
	
	VkSubmitInfo submit = vkinit::submitInfo(&drawCmdBuffers[ind]);
	submit.waitSemaphoreCount = 2;
	submit.pWaitSemaphores = graphicsWaitSemaphores;
	submit.pWaitDstStageMask = graphicsWaitStageMasks;
	submit.signalSemaphoreCount = 2;
	submit.pSignalSemaphores = graphicsSignalSemaphores;
	vkQueueSubmit(graphicsQueue, 1, &submit, renderFence);
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

void VulkanEngine::cleanup() {
	if (_isInitialized) {
		VkFence renderFences[] = {renderFence};

		//wait on ALL render fences (double buffering is trolling)
		vkWaitForFences(device, 1, renderFences, VK_TRUE, 1000000000);

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
	auto start = std::chrono::system_clock::now();
	ImGui::Render();

	//wait for render
	vkWaitForFences(device, 1, &renderFence, VK_TRUE, 10000000);
	vkResetFences(device, 1, &renderFence);

	//compute
	run_compute();

	//graphics
	uint32_t swapchainIndex;
	vkAcquireNextImageKHR(device, swapchain, 1000000000, presentSemaphore, nullptr, &swapchainIndex);
	run_graphics(swapchainIndex);

	auto end = std::chrono::system_clock::now();    
	auto fence = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	//present queue results
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderSemaphore;
	presentInfo.pImageIndices = &swapchainIndex;

	VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

	vkQueueWaitIdle(graphicsQueue);
	end = std::chrono::system_clock::now();    
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	//cout << "Frametime: " << elapsed.count() / 1000.f << "ms; fence wait: " << fence.count() / 1000.f << "ms\n";

	_frameNumber = rayTracerParams.progressive ? _frameNumber + 1 : 0;
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

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);
		ImGui::NewFrame();
		imgui_draw();

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