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
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");

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

	// for (int i = 0; i < triangles.size(); i++) {
	// 	TrianglePoint v0 = triPoints[triangles[i].v0];
	// 	TrianglePoint v1 = triPoints[triangles[i].v1];
	// 	TrianglePoint v2 = triPoints[triangles[i].v2];
	// 	TrianglePoint tris[] = {v0, v1, v2};
	// 	for (int j = 0; j < 3; j++) {
	// 		glm::vec4 p = tris[j].position;
	// 		glm::vec2 uv = {tris[j].position.w, tris[j].normal.w};
	// 		if (abs(p.x + 10.9128f) < 0.01f && abs(p.y + 5.72331f) < 0.01f && abs(p.z - 2.51851f) < 0.01f && j == 2) {
	// 			cout << uv.x << " " << uv.y << " " << i << endl;
	// 		}
	// 	}
	// }

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
		.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
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
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32},
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
		{VK_DESCRIPTOR_TYPE_SAMPLER, 2},
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
	VkDescriptorSetLayoutBinding textureBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1);
	VkDescriptorSetLayoutBinding sphereBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
	VkDescriptorSetLayoutBinding materialBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3);
	VkDescriptorSetLayoutBinding triPointBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 4);
	VkDescriptorSetLayoutBinding triangleBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 5);
	VkDescriptorSetLayoutBinding objectBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 6);
	VkDescriptorSetLayoutBinding bvhBufferBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 7);
	VkDescriptorSetLayoutBinding samplerBinding = vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 8);

	textureBufferBinding.descriptorCount = MAX_TEXTURES;
	samplerBinding.descriptorCount = 2;

	VkDescriptorSetLayoutBinding computeBindings[] = {computeBinding, sphereBufferBinding, materialBufferBinding, textureBufferBinding, triPointBufferBinding, triangleBufferBinding, objectBufferBinding, bvhBufferBinding, samplerBinding};

	VkDescriptorSetLayoutCreateInfo computeSetInfo{};
	computeSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	computeSetInfo.bindingCount = 9;
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

	VkDescriptorPool imguiPool; // rachit was here :)
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
	VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	vkCreateSampler(device, &samplerInfo, nullptr, &sampler);

	VkSampler clampSampler;
	VkSamplerCreateInfo clampSamplerInfo = vkinit::samplerCreateInfo(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	vkCreateSampler(device, &clampSamplerInfo, nullptr, &clampSampler);

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
	materialBufferInfo.range = sizeof(RayMaterial) * rayMaterials.size();

	VkDescriptorImageInfo textureImageInfos[MAX_TEXTURES];
	for (int i = 0; i < MAX_TEXTURES; i++) {
		//textureImageInfos[i].sampler = sampler;
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

	VkDescriptorBufferInfo bvhBufferInfo;
	bvhBufferInfo.buffer = bvhBuffer.buffer;
	bvhBufferInfo.offset = 0;
	bvhBufferInfo.range = sizeof(BVHNode) * bvhNodes.size();

	VkWriteDescriptorSet compTex = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, computeSet, &compImageInfo, 0);
	VkWriteDescriptorSet textureWrite = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, computeSet, textureImageInfos, 1);
	VkWriteDescriptorSet sphereWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &sphereBufferInfo, 2);
	VkWriteDescriptorSet materialWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &materialBufferInfo, 3);
	VkWriteDescriptorSet triPointWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &triPointBufferInfo, 4);
	VkWriteDescriptorSet triangleWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &triangleBufferInfo, 5);
	VkWriteDescriptorSet objectWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &objectBufferInfo, 6);
	VkWriteDescriptorSet bvhWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, computeSet, &bvhBufferInfo, 7);

	VkDescriptorImageInfo samplerImageInfos[2];
	for (int i = 0; i < 2; i++) {
		samplerImageInfos[i].sampler = i == 0 ? sampler : clampSampler;
		samplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}

	VkWriteDescriptorSet samplerSet{};
	samplerSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	samplerSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	samplerSet.dstSet = computeSet;
	samplerSet.dstBinding = 8;
	samplerSet.pImageInfo = samplerImageInfos;
	samplerSet.descriptorCount = 2;

	textureWrite.descriptorCount = MAX_TEXTURES;
	
	VkWriteDescriptorSet computeWrites[] = {compTex, textureWrite, sphereWrite, materialWrite, triPointWrite, triangleWrite, objectWrite, bvhWrite, samplerSet};

	vkUpdateDescriptorSets(device, 9, computeWrites, 0, nullptr);

	deletionQueue.push_function([=]() {
		vkDestroySampler(device, sampler, nullptr);
		vkDestroySampler(device, clampSampler, nullptr);
	});
}

void VulkanEngine::cornell_box() {
	ImGuiObject light;
	light.frontOnly = true;
	light.name = "light";
	light.position = glm::vec3(0.f, -1.5f, 0.f);
	light.scale = glm::vec3(1.f);
	read_obj("../assets/light2.obj", light, 3);

	ImGuiObject plane;
	plane.frontOnly = true;
	plane.name = "bottom";
	plane.position = glm::vec3(0.f, 0.5f, 0.f);
	plane.rotation = glm::vec3(0.f);
	read_obj("../assets/plane.obj", plane, 0);

	plane.name = "left";
	plane.position = glm::vec3(-1.f, -0.5f, 0.f);
	plane.rotation = glm::vec3(90.f, 0.f, 90.f);
	read_obj("../assets/plane.obj", plane, 2);

	plane.name = "right";
	plane.position = glm::vec3(1.f, -0.5f, 0.f);
	plane.rotation = glm::vec3(90.f, 0.f, -90.f);
	read_obj("../assets/plane.obj", plane, 1);

	plane.name = "top";
	plane.position = glm::vec3(0.f, -1.5f, 0.f);
	plane.rotation = glm::vec3(0.f, 0.f, 0.f);
	read_obj("../assets/ceiling.obj", plane, 0);

	plane.name = "back";
	plane.position = glm::vec3(0.f, -0.5f, 1.f);
	plane.rotation = glm::vec3(90.f, 0.f, 0.f);
	plane.scale = glm::vec3(1.f);
	read_obj("../assets/plane.obj", plane, 0);

	plane.name = "front";
	plane.position = glm::vec3(0.f, -0.5f, -1.f);
	plane.rotation = glm::vec3(-90.f, 0.f, 0.f);
	read_obj("../assets/plane.obj", plane, 0);
}

void VulkanEngine::prepare_storage_buffers() {
	//spheres
	spheres.resize(MAX_SPHERES);

	//spheres[0] = {glm::vec3(0.f, 0.1f, -0.3f), 0.4f, 5};
	//spheres[1] = {glm::vec3(0.5f, 0.1f, 0.f), 0.4f, 2};
	copy_buffer(sizeof(Sphere) * MAX_SPHERES, sphereBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) spheres.data());

	//materials
	RayMaterial dielectric;
	dielectric.ior = 2.f;
	dielectric.albedo = glm::vec3(1.f);

	RayMaterial mirror;
	mirror.reflectance = 1.f;

	RayMaterial white;

	RayMaterial red;
	red.albedo = glm::vec3(1.f, 0.f, 0.f);

	RayMaterial green;
	green.albedo = glm::vec3(0.f, 1.f, 0.f);

	RayMaterial blue;
	blue.albedo = glm::vec3(0.f, 0.f, 1.f);

	RayMaterial li;
	li.emissionColor = glm::vec3(1.f, 1.f, 1.f);
	li.albedo = glm::vec3(0.f);
	li.emissionStrength = 2.4f;

	RayMaterial object;
	object.albedoIndex = -1;
	object.metalnessIndex = -1;
	//object.ior = 1.5f;

	rayMaterials.push_back(white);
	rayMaterials.push_back(red);
	rayMaterials.push_back(green);
	rayMaterials.push_back(li);
	rayMaterials.push_back(mirror);
	rayMaterials.push_back(dielectric);
	// rayMaterials.push_back(object);

	//ccw
	ImGuiObject model;
	model.name = "sponza";
	model.scale = glm::vec3(1.f);
	//read_obj("../assets/sponza2/sponza_tri.obj", model, 0);

	model.name = "cube";
	model.scale = glm::vec3(0.25f);
	model.samplerIndex = 1;
	model.rotation = glm::vec3(0.f, -30.f, 0.f);
	model.position = glm::vec3(-0.4f, 0.25f, -0.45f);
	read_obj("../assets/cube.obj", model, 0);

	model.name = "cube2";
	model.scale = glm::vec3(0.3f, 0.7f, 0.3f);
	model.samplerIndex = 1;
	model.rotation = glm::vec3(0.f, 30.f, 0.f);
	model.position = glm::vec3(0.4f, -0.2f, 0.45f);
	read_obj("../assets/cube.obj", model, 0);

	model.name = "bunny";
	model.scale = glm::vec3(0.7f);
	model.samplerIndex = 1;
	model.position = glm::vec3(0.f, 0.53f, 0.f);
	//read_obj("../assets/bunny_full.obj", model, 5);

	cornell_box();

	copy_buffer(sizeof(RayMaterial) * rayMaterials.size(), materialBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) rayMaterials.data());
	copy_buffer(sizeof(TrianglePoint) * triPoints.size(), triPointBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) triPoints.data());
	copy_buffer(sizeof(Triangle) * triangles.size(), triangleBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) triangles.data());
	copy_buffer(sizeof(RenderObject) * objects.size(), objectBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) objects.data());
	copy_buffer(sizeof(BVHNode) * bvhNodes.size(), bvhBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) bvhNodes.data());
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		cout << "cannot find file " << filePath << endl;
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

void VulkanEngine::read_obj(std::string filePath, ImGuiObject imGuiObj, int material) {
	//dont store the same tris, reuse bvh
	if (loadedObjects.count(filePath) != 0) {
		RenderObject object;
		object.materialIndex = material;
		object.smoothShade = false;
		object.bvhIndex = loadedObjects.at(filePath);
		object.transformMatrix = glm::translate(imGuiObj.position) * 
			glm::rotate(glm::radians(imGuiObj.rotation.x), glm::vec3(1.f, 0.f, 0.f)) * 
			glm::rotate(glm::radians(imGuiObj.rotation.y), glm::vec3(0.f, 1.f, 0.f)) * 
			glm::rotate(glm::radians(imGuiObj.rotation.z), glm::vec3(0.f, 0.f, 1.f)) *
			glm::scale(imGuiObj.scale);
		objects.push_back(object);
		imGuiObjects.push_back(imGuiObj);
		return;
	}

	int pointOffset = triPoints.size();
	int triOffset = triangles.size();
	int objectTriOffset = triangles.size();
	std::ifstream fileStream;
	fileStream.open(filePath);
	auto start = std::chrono::system_clock::now();

	bool smoothShade = false;
	bool includeUVs = false;
	std::string currentMat;
	std::string fileLine;
	std::string materialFile;
	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> uvs;
	std::vector<glm::vec3> normals;
	BoundingBox bounds;

	if (!fileStream.is_open()) return;
	while (fileStream) {
		std::getline(fileStream, fileLine);
		if (fileLine.find("mtllib") != std::string::npos) {
			materialFile = fileLine.substr(7, fileLine.size() - 7);
			std::string mtlPath = filePath.substr(0, filePath.rfind("/") + 1);
			read_mtl(mtlPath + materialFile);
		}

		std::string prefix = fileLine.substr(0, fileLine.find(' '));

		if (prefix == "v") { //vertices
			int index = 2;
			glm::vec3 position;
			for (int i = 0; i < 3; i++) {
				int nextSpace = fileLine.find(' ', index);
				position[i] = stof(fileLine.substr(index, nextSpace - index));
				index = nextSpace + 1;
			}
			bounds.grow(position);
			scene.grow(position);
			positions.push_back(glm::vec4(position, 0.f));
		} else if (prefix == "vt") { //uv
			glm::vec2 uv;
			int firstSpace = fileLine.find(' ', 2);
			int secondSpace = fileLine.find(' ', firstSpace + 1);
			float u = stof(fileLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
			float v = stof(fileLine.substr(secondSpace, fileLine.length() - secondSpace));
			uv.x = u;
			uv.y = v;
			uvs.push_back(uv);
		} else if (prefix == "vn") { //normal
			int index = 3;
			glm::vec3 normal;
			for (int i = 0; i < 3; i++) {
				int nextSpace = fileLine.find(' ', index);
				normal[i] = stof(fileLine.substr(index, nextSpace - index));
				index = nextSpace + 1;
			}
			normals.push_back(normal);
		} else if (prefix == "f") { //triangles
			int index = 0;

			std::vector<uint> vertexInd;
			std::vector<uint> normalInd;
			std::vector<uint> textureInd;

			int pointCount = 0;
			for (int i = 0; i < fileLine.size() - 1; i++) {
				if (fileLine.at(i) == ' ') pointCount++;
			}

			for (int i = 0; i < pointCount; i++) {
				int space = fileLine.find(' ', index);
				int nextSpace = fileLine.find(' ', space + 1);
				std::string vertex = fileLine.substr(space + 1, nextSpace - space - (i == pointCount - 1 ? 0 : 1));

				int firstSlash = vertex.find('/');
				int secondSlash = vertex.find('/', firstSlash + 1);

				std::string vIndexStr = vertex.substr(0, firstSlash);
				if (!vIndexStr.empty()) {
					vertexInd.push_back(stoi(vIndexStr) - 1);
					//POINT OFFSET
				}

				std::string uvIndexStr = vertex.substr(firstSlash + 1, secondSlash - firstSlash - 1);
				if (!uvIndexStr.empty()) {
					textureInd.push_back(stoi(uvIndexStr) - 1);
					includeUVs = true;
				}

				if (normals.size() == 0) continue;
				std::string nIndexStr = vertex.substr(secondSlash + 1, vertex.size() - secondSlash - 1);
				if (!nIndexStr.empty()) {
					normalInd.push_back(stoi(nIndexStr) - 1);
				}

				index = nextSpace;
			}


			glm::uvec4 pointIndex;
			
			//put uv in the vec4s
			for (int i = 0; i < pointCount; i++) {
				glm::vec3 normal;
				if (normals.size() == 0) {
					normal = glm::vec3(0.f);
				} else {
					normal = normals[normalInd[i]];
				}
				glm::vec2 uv = includeUVs ? uvs[textureInd[i]] : glm::vec2(0.f);
				glm::vec3 p = positions[vertexInd[i]];

				TrianglePoint point;
				point.position = glm::vec4(p, uv.x);
				point.normal = glm::vec4(normal, uv.y);

				pointIndex[i] = triPoints.size();
				triPoints.push_back(point);
			}
			
			glm::vec3 tangent, binormal;

			calculate_binormal(pointIndex[0], pointIndex[1], pointIndex[2], tangent, binormal);

			Triangle tri;
			tri.v0 = pointIndex[0];
			tri.v1 = pointIndex[1];
			tri.v2 = pointIndex[2];
			tri.frontOnly = imGuiObj.frontOnly;
			tri.tangent = tangent;
			tri.binormal = binormal;
			
			TrianglePoint tp[] = {triPoints[pointIndex[0]], triPoints[pointIndex[1]], triPoints[pointIndex[2]]};
			glm::vec3 centroid = glm::vec3(0.f);
			for (int i = 0; i < 3; i++) {
				glm::vec4 p = tp[i].position;
				centroid.x += p.x;
				centroid.y += p.y;
				centroid.z += p.z;
			}

			triangles.push_back(tri);
			centroids.push_back(centroid / 3.f);
		} else if (prefix == "usemtl") {
			int space = fileLine.find(' ');
			std::string mat = fileLine.substr(space + 1, fileLine.size() - space - 1);
			if (currentMat.empty()) {
				currentMat = mat;
				continue;
			}
			//create object
			RenderObject object;

			std::string mtlPath = filePath.substr(0, filePath.rfind("/") + 1);
			object.materialIndex = currentMat.empty() ? material : loadedMaterials.at(mtlPath + materialFile + "/" + currentMat);
			object.transformMatrix = glm::translate(imGuiObj.position) * 
				glm::rotate(glm::radians(imGuiObj.rotation.x), glm::vec3(1.f, 0.f, 0.f)) * 
				glm::rotate(glm::radians(imGuiObj.rotation.y), glm::vec3(0.f, 1.f, 0.f)) * 
				glm::rotate(glm::radians(imGuiObj.rotation.z), glm::vec3(0.f, 0.f, 1.f)) *
				glm::scale(imGuiObj.scale);
			object.smoothShade = smoothShade; //FIX
			object.bvhIndex = bvhNodes.size();
			object.samplerIndex = imGuiObj.samplerIndex;
			objects.push_back(object);

			imGuiObjects.push_back(imGuiObj);
			imGuiObjects.at(imGuiObjects.size() - 1).name += "/" + currentMat;

			loadedObjects.emplace(filePath + "/" + currentMat, object.bvhIndex);

			glm::mat4 inverse = glm::inverse(object.transformMatrix);
			bounds.bounds[0] = glm::vec4(-1920.95f, -1429.43f, -1105.43f, 1.f); 
			bounds.bounds[1] = glm::vec4(1799.91f, 126.433f, 1182.81f, 1.f); 

			bounds.bounds[0] = inverse * scene.bounds[0];
			bounds.bounds[1] = inverse * scene.bounds[1];

			cout << endl << filePath << " " << currentMat << endl; 

			build_bvh(triangles.size() - objectTriOffset, objectTriOffset, bounds);

			//RESET
			currentMat = mat;
			objectTriOffset = triangles.size();
			bounds = {};
			smoothShade = false;
		} else if (prefix == "s") {
			int smooth = fileLine.at(2) - '0'; //converts ascii to int
			smoothShade = smooth == 1; //do this later
		}
	}

	RenderObject object;
	std::string mtlPath = filePath.substr(0, filePath.rfind("/") + 1);
	object.materialIndex = currentMat.empty() ? material : loadedMaterials.at(mtlPath + materialFile + "/" + currentMat);
	object.transformMatrix = glm::translate(imGuiObj.position) * 
		glm::rotate(glm::radians(imGuiObj.rotation.x), glm::vec3(1.f, 0.f, 0.f)) * 
		glm::rotate(glm::radians(imGuiObj.rotation.y), glm::vec3(0.f, 1.f, 0.f)) * 
		glm::rotate(glm::radians(imGuiObj.rotation.z), glm::vec3(0.f, 0.f, 1.f)) *
		glm::scale(imGuiObj.scale);
	object.smoothShade = smoothShade;
	object.bvhIndex = bvhNodes.size();
	objects.push_back(object);
	imGuiObjects.push_back(imGuiObj);

	loadedObjects.emplace(filePath, object.bvhIndex);

	glm::mat4 inverse = glm::inverse(object.transformMatrix);
	bounds.bounds[0] = glm::vec4(-1920.95f, -1429.43f, -1105.43f, 1.f); 
	bounds.bounds[1] = glm::vec4(1799.91f, 126.433f, 1182.81f, 1.f); 

	bounds.bounds[0] = inverse * bounds.bounds[0];
	bounds.bounds[1] = inverse * bounds.bounds[1];

	cout << endl << filePath << " " << currentMat << endl; 
	build_bvh(triangles.size() - objectTriOffset, objectTriOffset, bounds);

	auto end = std::chrono::system_clock::now();    
	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	cout << "> Object at " << filePath << ": " << triangles.size() - triOffset << " tris, " << triPoints.size() - pointOffset << " verts, " << time.count() << "ms total load time " << endl;
}

//https://stackoverflow.com/questions/5255806/how-to-calculate-tangent-and-binormal/5257471#5257471
void VulkanEngine::calculate_binormal(int v1, int v2, int v3, glm::vec3& tangent, glm::vec3& binormal) {
	glm::vec4 a = triPoints[v1].position;
	glm::vec4 b = triPoints[v2].position;
	glm::vec4 c = triPoints[v3].position;

	glm::vec2 h = glm::vec2(a.w, triPoints[v1].normal.w);
	glm::vec2 k = glm::vec2(b.w, triPoints[v2].normal.w);
	glm::vec2 l = glm::vec2(c.w, triPoints[v3].normal.w);

	glm::vec3 edge1 = glm::vec3(b.x, b.y, b.z) - glm::vec3(a.x, a.y, a.z); 
	glm::vec3 edge2 = glm::vec3(c.x, c.y, c.z) - glm::vec3(a.x, a.y, a.z);
	glm::vec2 uv1 = k - h;
	glm::vec2 uv2 = l - h;

	glm::mat2 cup = glm::transpose(glm::mat2(uv1, uv2));
	// if (h == k || k == l || l == h) {
	// 	cout << glm::to_string(h) << " " << glm::to_string(k) << " " << glm::to_string(l) << endl;
	// }
}

void VulkanEngine::read_mtl(std::string filePath) {
	std::ifstream fileStream;
	fileStream.open(filePath);

	if (!fileStream.is_open()) {
		cout << "Could not open material file: " << filePath << endl;
		return;
	}

	std::string fileLine;
	std::string materialName;
	RayMaterial currentMaterial;
	std::vector<std::string> imageFilePaths;
	std::vector<AllocatedImage*> allocatedImages;

	while (fileStream) {
		std::getline(fileStream, fileLine);
		if (fileLine.find("newmtl") != std::string::npos) {
			if (!materialName.empty()) {
				loadedMaterials.emplace(filePath + "/" + materialName, rayMaterials.size());
				rayMaterials.push_back(currentMaterial);
				currentMaterial = {};
			}
			materialName = fileLine.substr(7, fileLine.size() - 7);
			continue;
		}

		std::string mtlPath = filePath.substr(0, filePath.rfind("/") + 1);
		fileLine.erase(remove(fileLine.begin(), fileLine.end(), '\t'), fileLine.end()); //remove tabs from the beggining
		std::string prefix = fileLine.substr(0, fileLine.find(' '));
		if (prefix == "Ka" || prefix == "Kd") {
			int space1 = fileLine.find(' ');
			int space2 = fileLine.find(' ', space1 + 1);
			int space3 = fileLine.find(' ', space2 + 1);

			std::string r = fileLine.substr(space1 + 1, space2 - space1 - 1);
			std::string g = fileLine.substr(space2 + 1, space3 - space2 - 1);
			std::string b = fileLine.substr(space3 + 1, fileLine.size() - space3 - 1);

			glm::vec3 color = glm::vec3(stof(r), stof(g), stof(b));
			currentMaterial.albedo *= color;
		} else if (prefix == "Ni") {
			int space = fileLine.find(' ');
			std::string value = fileLine.substr(space + 1, fileLine.size() - space - 1);
			//currentMaterial.ior = stof(value);
		} else if (prefix == "d") {
			int space = fileLine.find(' ');
			std::string value = fileLine.substr(space + 1, fileLine.size() - space - 1);
			//alpha = stof(value);
		} else if (prefix == "map_Ka" || prefix == "map_Kd") {
			int space = fileLine.find(' ');
			std::string value = fileLine.substr(space + 1, fileLine.size() - space - 1);
			std::string path = mtlPath + value;
			imageFilePaths.push_back(path);
			allocatedImages.push_back(&textures[texturesUsed].image);
			currentMaterial.albedoIndex = texturesUsed;
			texturesUsed++;
		} else if (prefix == "map_Ks") {
			int space = fileLine.find(' ');
			std::string value = fileLine.substr(space + 1, fileLine.size() - space - 1);
			std::string path = mtlPath + value;
			imageFilePaths.push_back(path);
			allocatedImages.push_back(&textures[texturesUsed].image);
			currentMaterial.metalnessIndex = texturesUsed;
			texturesUsed++;
		} else if (prefix == "map_d") {
			int space = fileLine.find(' ');
			std::string value = fileLine.substr(space + 1, fileLine.size() - space - 1);
			std::string path = mtlPath + value;
			imageFilePaths.push_back(path);
			allocatedImages.push_back(&textures[texturesUsed].image);
			currentMaterial.alphaIndex = texturesUsed;
			texturesUsed++;
		} else if (prefix == "map_bump") {
			int space = fileLine.find(' ');
			std::string value = fileLine.substr(space + 1, fileLine.size() - space - 1);
			std::string path = mtlPath + value;
			imageFilePaths.push_back(path);
			allocatedImages.push_back(&textures[texturesUsed].image);
			currentMaterial.bumpIndex = texturesUsed;
			texturesUsed++;
		}
	}

	//last material since it only pushes with new mtl line
	loadedMaterials.emplace(filePath + "/" + materialName, rayMaterials.size());
	rayMaterials.push_back(currentMaterial);

	//string pointer jank idk
	const char* chars[imageFilePaths.size()];
	for (int i = 0; i < imageFilePaths.size(); i++) {
		const char* c = imageFilePaths[i].c_str();
		chars[i] = c;
	}

	vkutil::load_images_from_file(*this, chars, allocatedImages.data(), imageFilePaths.size());
	for (int i = texturesUsed - allocatedImages.size(); i < texturesUsed; i++) {
		vkDestroyImageView(device, textures[i].imageView, nullptr);
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textures[i].image.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &textures[i].imageView));
	}

	deletionQueue.push_function([=]() {
        for (AllocatedImage* image : allocatedImages) {
            vmaDestroyImage(allocator, image->image, image->allocation);
		}
    });
}

void VulkanEngine::build_bvh(int size, int triIndex, BoundingBox scene) {
	auto start = std::chrono::system_clock::now();

	nodesUsed++;
	int offset = bvhNodes.size();
	bvhNodes.resize(bvhNodes.size() + (size * 2 - 1));
	BVHNode& root = bvhNodes[offset];
	root.index = triIndex;
	root.triCount = size;

	BVHStats stats;

	update_bvh_bounds(offset);
	subdivide_bvh(offset, 0, stats, scene);

	bvhNodes.resize(nodesUsed);
	bvhNodes.shrink_to_fit();	

	auto end = std::chrono::system_clock::now();    
	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	cout << "BVH Build Time: " << time.count() << "ms\n";
	cout << "Node Count: " << nodesUsed - offset << endl;
	cout << "Max Depth: " << stats.maxDepth << endl;
	cout << "Min Depth: " << stats.minDepth << endl;
	cout << "Max Tris: " << stats.maxTri << endl;
}

void VulkanEngine::update_bvh_bounds(uint index) {
	BVHNode& node = bvhNodes[index];
	BoundingBox box;

	for (int i = 0; i < node.triCount; i++) {
		Triangle& leafTri = triangles[node.index + i];
		box.grow(triPoints[leafTri.v0]);
		box.grow(triPoints[leafTri.v1]);
		box.grow(triPoints[leafTri.v2]);
	}

	node.boundsX = glm::vec2(box.bounds[0].x, box.bounds[1].x);
	node.boundsY = glm::vec2(box.bounds[0].y, box.bounds[1].y);
	node.boundsZ = glm::vec2(box.bounds[0].z, box.bounds[1].z);
}

void VulkanEngine::subdivide_bvh(uint index, uint depth, BVHStats& stats, BoundingBox scene) {
	BVHNode& node = bvhNodes[index];
	
	if (node.triCount <= 2 || depth >= 64) {
		stats.maxDepth = iMax(depth, stats.maxDepth);
		stats.minDepth = iMin(depth, stats.minDepth);
		stats.maxTri = iMax(node.triCount, stats.maxTri);
		return;
	}

	int axis = 0;
	float splitPos = 0.f;
	float bestCost = find_bvh_split_plane(node, axis, splitPos, scene);	

    BoundingBox parent;
	parent.bounds[0] = glm::vec4(node.boundsX[0], node.boundsY[0], node.boundsZ[0], 0.f);
	parent.bounds[1] = glm::vec4(node.boundsX[1], node.boundsY[1], node.boundsZ[1], 0.f);
    float noSplitCost = node.triCount * scene_interior_cost(parent, scene);
	if (bestCost >= noSplitCost) {
		stats.maxDepth = iMax(depth, stats.maxDepth);
		stats.minDepth = iMin(depth, stats.minDepth);
		stats.maxTri = iMax(node.triCount, stats.maxTri);
		return;
	}

	//partition the triangles
	int i = node.index;
	int j = i + node.triCount - 1;
	while (i <= j) {
		glm::vec3 centroid = centroids[i];

		//swap so left side of array is less than splitPos
		if (centroid[axis] < splitPos) {
			i++;
		} else {
			swap(triangles[i], triangles[j]);
			swap(centroids[i], centroids[j]);
			j--;
		}
	}

	//if one side has all tris, abort
	int triIndex = node.index;
	int leftCount = i - triIndex;
	if (leftCount == 0 || leftCount == node.triCount) {
		stats.maxDepth = iMax(depth, stats.maxDepth);
		stats.minDepth = iMin(depth, stats.minDepth);
		stats.maxTri = iMax(node.triCount, stats.maxTri);
		return;
	}

	//node.index is always at first a tri ifor (int index, only becomes a node index after a split
	node.index = nodesUsed;
	nodesUsed += 2; //right node increase
	bvhNodes[node.index].index = triIndex;
	bvhNodes[node.index].triCount = leftCount;
	bvhNodes[node.index + 1].index = i;
	bvhNodes[node.index + 1].triCount = node.triCount - leftCount;

	node.triCount = 0;
	update_bvh_bounds(node.index);
	update_bvh_bounds(node.index + 1);

	subdivide_bvh(node.index, depth + 1, stats, scene);
	subdivide_bvh(node.index + 1, depth + 1, stats, scene);
}

float VulkanEngine::find_bvh_split_plane(BVHNode& node, int& axis, float& splitPos, BoundingBox scene) {
	float bestCost = 1e30f;
	for (int a = 0; a < 3; a++) {
		float min = 1e30f;
		float max = -1e30f;
		for (int i = 0; i < node.triCount; i++) {
			min = iMin(min, centroids[node.index + i][a]);
			max = iMax(max, centroids[node.index + i][a]);
		}

		if (min == max) continue;

		//populate bins
		BVHBin bins[BINS];
		float scale = BINS / (max - min);
		for (int i = 0; i < node.triCount; i++) {
			Triangle tri = triangles[node.index + i];
			int binIndex = iMin(BINS - 1, floor((centroids[node.index + i][a] - min) * scale));
			bins[binIndex].triCount++;
			bins[binIndex].box.grow(triPoints[tri.v0]);
			bins[binIndex].box.grow(triPoints[tri.v1]);
			bins[binIndex].box.grow(triPoints[tri.v2]);
		}

		//data for planes between the bins, loop through to find each
		float leftArea[BINS - 1];
		float rightArea[BINS - 1];
		float leftCount[BINS - 1];
		float rightCount[BINS - 1];
		BoundingBox leftBox;
		BoundingBox rightBox;
		int leftSum = 0;
		int rightSum = 0;
		
		for (int i = 0; i < BINS - 1; i++) {
			leftSum += bins[i].triCount;
			leftCount[i] = leftSum;
			leftBox.grow(bins[i].box);
			leftArea[i] = scene_interior_cost(leftBox, scene);
			rightSum += bins[BINS - 1 - i].triCount;
			rightCount[BINS - 2 - i] = rightSum;
			rightBox.grow(bins[BINS - 1 - i].box);
			rightArea[i] = rightBox.surfaceArea();
			rightArea[BINS - 2 - i] = scene_interior_cost(rightBox, scene);
		}

		scale = (max - min) / BINS;
		for (int i = 0; i < BINS - 1; i++) {
			float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
			if (cost < bestCost) {
				axis = a;
				splitPos = min + scale * (i + 1);
				bestCost = cost;
			}
		}
	}

	return bestCost;
}

//https://diglib.eg.org/server/api/core/bitstreams/0e178688-ff5b-44ff-b660-1c3259c23b0c/content
float VulkanEngine::scene_interior_cost(BoundingBox node, BoundingBox scene) {
	return node.surfaceArea();
	// REMEMBER TO DO ROTATION
	glm::vec4 extent = (node.bounds[1]) - (node.bounds[0]);

	float inv_volume = 1 / scene.volume();
	float ben = node.volume() * inv_volume; //ben goodman
	
	//i dont know man
	long double sum = 0.0;
	for (int i = 0; i < 3; i++) {
		float surface = 0.f;
		if (i == 0) {
			surface = extent.y * extent.z;
		} else if (i == 1) {
			surface = extent.x * extent.z;
		} else {
			surface = extent.x * extent.y;
		}

		for (int j = 0; j < 2; j++) {
			BoundingBox prime;
			prime.bounds[0] = scene.bounds[0];
			prime.bounds[1] = scene.bounds[1];
			prime.bounds[j][i] = node.bounds[1 - j][i];
			sum += prime.volume() * surface / prime.surfaceArea();
			if (prime.volume() * surface / prime.surfaceArea() < 0) {
				cout << prime.volume() << "BAD" << endl;
				exit(0);
			} 
		}
	}
	return ben + inv_volume * sum;
}

void VulkanEngine::init_image() {
	textures.resize(MAX_TEXTURES);

	vkutil::create_empty_image(*this, computeImage.image, _windowExtent);

	VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, computeImage.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &computeImage.imageView));

	//im stupid and bad at memory
	AllocatedImage textureImages[MAX_TEXTURES];
	vkutil::create_empty_images(*this, textureImages, _windowExtent, MAX_TEXTURES);
	for (int i = 0; i < MAX_TEXTURES; i++) {
		textures[i].image = textureImages[i];
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, textures[i].image.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &textures[i].imageView));
	}

	deletionQueue.push_function([=]() {
		vkDestroyImageView(device, computeImage.imageView, nullptr);
		for (int i = 0; i < MAX_TEXTURES; i++) {
			vmaDestroyImage(allocator, textureImages[i].image, textureImages[i].allocation);
			vkDestroyImageView(device, textures[i].imageView, nullptr);
		}
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
		ImGui::Checkbox("Automatic Progressive Rendering", &autoProgressive);
		ImGui::Checkbox("Single Rendering", &rayTracerParams.singleRender);

		float sampleProgress = (float) totalSamples / rayTracerParams.sampleLimit;
		glm::vec4 c = glm::mix(glm::vec4(1.f, 0.f, 0.f, 1.f), glm::vec4(0.f, 1.f, 0.f, 1.f), sampleProgress);

		ImGui::TextColored({c.r, c.g, c.b, c.a}, "Single Render Progress: %.1f%%", 100 * sampleProgress);
		ImGui::SliderInt("Debug Mode", &rayTracerParams.debug, -1, 2, "%d");
		ImGui::DragInt("Rays Per Pixel", (int*) &rayTracerParams.raysPerPixel, 1.f, 0, 1000);
		ImGui::DragInt("Bounce Limit", (int*) &rayTracerParams.bounceLimit, 1.f, 0, 100);
		ImGui::DragInt("Triangle Test Threshold", (int*) &rayTracerParams.triangleCap, 1.f, 0);
		ImGui::DragInt("Box Test Threshold", (int*) &rayTracerParams.boxCap, 1.f, 0);
		ImGui::DragInt("Sample Limit", (int*) &rayTracerParams.sampleLimit, 1.f, 0);
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
		ImGui::DragFloat("Sun Focus", &environment.horizonColor.w, 0.1f, 0.f, 100.f);
		ImGui::DragFloat("Sun Intensity", &environment.zenithColor.w, 0.1f, 0.f, 100.f);
	}

	if (ImGui::CollapsingHeader("Materials")) {
		ImGui::Indent(16.f);

		ImGui::Unindent(4.f);
		if (ImGui::Button("Add Material") && rayMaterials.size() < MAX_MATERIALS) {
			rayMaterials.push_back({});
		}

		if (ImGui::Button("Update Buffer")) {
			update_buffer(sizeof(RayMaterial) * rayMaterials.size(), materialBuffer, rayMaterials.data());
		}
		ImGui::Indent(4.f);

		for (int i = 0; i < rayMaterials.size(); i++) {
			if (ImGui::CollapsingHeader(("Material " + to_string(i)).c_str())) {
				ImGui::Indent(16.f);
				ImGui::ColorEdit3("Albedo", (float*) &rayMaterials[i].albedo);
				ImGui::ColorEdit3("Emission Color", (float*) &rayMaterials[i].emissionColor);
				ImGui::DragFloat("Emission Strength", (float*) &rayMaterials[i].emissionStrength, 0.1f, 0.f, 100.f);
				ImGui::DragFloat("Reflectance", &rayMaterials[i].reflectance, 0.05f, 0.f, 1.f);
				ImGui::DragFloat("Index of Refraction", &rayMaterials[i].ior, 0.01f, 0.f, 10.f);
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
					glm::rotate(glm::radians(object.rotation.z), glm::vec3(0.f, 0.f, 1.f)) *
					glm::scale(object.scale);
			}
			update_buffer(sizeof(RenderObject) * objects.size(), objectBuffer, objects.data());
		}
		ImGui::Indent(4.f);

		for (int i = 0; i < imGuiObjects.size(); i++) {
			if (ImGui::CollapsingHeader(imGuiObjects[i].name.c_str())) {
				ImGui::Indent(16.f);
				ImGui::DragFloat3("Position", (float*) &imGuiObjects[i].position, 0.1f);
				ImGui::DragFloat3("Rotation", (float*) &imGuiObjects[i].rotation, 1.f);
				ImGui::DragFloat3("Scale", (float*) &imGuiObjects[i].scale, 1.f);
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
	cameraInfo.cameraRotation = rotY * rotX * rotZ;

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
	VkSemaphore graphicsWaitSemaphores[] = {presentSemaphore, computeSemaphore};
	VkSemaphore graphicsSignalSemaphores[] = {renderSemaphore, graphicsSemaphore};
	
	VkSubmitInfo submit = vkinit::submitInfo(&drawCmdBuffers[ind]);
	submit.waitSemaphoreCount = totalSamples < rayTracerParams.sampleLimit ? 2 : 1;
	submit.pWaitSemaphores = graphicsWaitSemaphores;
	submit.pWaitDstStageMask = graphicsWaitStageMasks;
	submit.signalSemaphoreCount = totalSamples < rayTracerParams.sampleLimit ? 2 : 0;
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

	if (totalSamples < rayTracerParams.sampleLimit) {
		//compute
		run_compute();
	}

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
	if (totalSamples < rayTracerParams.sampleLimit) {
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderSemaphore;
	}
	presentInfo.pImageIndices = &swapchainIndex;

	VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

	vkQueueWaitIdle(graphicsQueue);
	end = std::chrono::system_clock::now();    
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	_frameNumber = rayTracerParams.progressive ? _frameNumber + 1 : 0;
	totalSamples += (totalSamples < rayTracerParams.sampleLimit ? rayTracerParams.sampleLimit : 0);
	if (!rayTracerParams.singleRender) totalSamples = 0;
}

void VulkanEngine::run() {
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit) {
		const Uint8* keyState;

		auto start = std::chrono::system_clock::now();
		bool movingMouse = false;

		// Handle events on queue
		while (SDL_PollEvent(&e) != 0) {
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_QUIT) {
				bQuit = true;
			}
			
			if (e.type == SDL_MOUSEBUTTONDOWN) {
				clicking = true;
			} else if (e.type == SDL_MOUSEBUTTONUP) {
				clicking = false;
			}

			ImGuiIO& io = ImGui::GetIO();

			if (e.type == SDL_MULTIGESTURE) {
				if (e.mgesture.numFingers == 2 && !clicking && !io.WantCaptureMouse) {
					if (prevMouseScroll == glm::vec2(0.f)) {
						prevMouseScroll.y = e.mgesture.y;
						prevMouseScroll.x = e.mgesture.x;
					}

					float dy = e.mgesture.y - prevMouseScroll.y;
					float dx = e.mgesture.x - prevMouseScroll.x;
					cameraAngles[0] += dy * mouseSensitivity;
					cameraAngles[1] += -dx * mouseSensitivity * 1.6667f;
					prevMouseScroll.y = e.mgesture.y;
					prevMouseScroll.x = e.mgesture.x;
					movingMouse = true;
				}
			} else if (e.type == SDL_FINGERUP) {
				prevMouseScroll = glm::vec2(0.f);
				movingMouse = false;
			}

			keyState = SDL_GetKeyboardState(NULL);
			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		glm::vec3 movement = glm::vec3(0.f); 

		if (keyState[SDL_SCANCODE_W]) {
			movement.z++;
		}

		if (keyState[SDL_SCANCODE_S]) {
			movement.z--;
		}

		if (keyState[SDL_SCANCODE_A]) {
			movement.x--;
		}

		if (keyState[SDL_SCANCODE_D]) {
			movement.x++;
		}

		if (movement != glm::vec3(0.f)) {
			movement = normalize(cameraInfo.cameraRotation * glm::vec4(movement, 0.f));
			cameraInfo.pos += movement * renderStats.frameTime * 0.001f * cameraSpeed;
			rayTracerParams.progressive = false;
		} else if (autoProgressive) {
			rayTracerParams.progressive = !movingMouse;
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