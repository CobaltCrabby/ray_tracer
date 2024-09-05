
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
	init_pipelines();

	load_meshes();

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
	vkb::Device vkbDevice = deviceBuilder.build().value();

	device = vkbDevice.device;
	this->physicalDevice = physicalDevice.physical_device;

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
	VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

	VkCommandBufferAllocateInfo commandBufferInfo = vkinit::commandBufferAllocateInfo(commandPool);
	VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferInfo, &commandBuffer));

	deletionQueue.push_function([=]() {
		vkDestroyCommandPool(device, commandPool, nullptr);
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
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &renderFence));

	deletionQueue.push_function([=]() {
		vkDestroyFence(device, renderFence, nullptr);
	});

	VkSemaphoreCreateInfo semaphoreInfo = vkinit::semaphoreCreateInfo();
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderSemaphore));
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &presentSemaphore));

	deletionQueue.push_function([=]() {
        vkDestroySemaphore(device, presentSemaphore, nullptr);
        vkDestroySemaphore(device, renderSemaphore, nullptr);
    });
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

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &trianglePipelineLayout));

	//create graphics pipeline
	PipelineBuilder builder;

	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertex));
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragment));

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
	builder._pipelineLayout = trianglePipelineLayout;

	trianglePipeline = builder.build_pipeline(device, renderPass);

	//use the same builder but different shaders
	builder._shaderStages.clear();
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, redVertex));
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, redFragment));

	redTrianglePipeline = builder.build_pipeline(device, renderPass);

	//mesh pipeline
	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	builder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	builder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
	builder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	builder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	builder._shaderStages.clear();

	VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();

	VkPushConstantRange pushConstant;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(MeshPushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(device, &meshPipelineLayoutInfo, nullptr, &meshPipelineLayout));
	VkShaderModule meshVertex;
	if (!load_shader_module((bin + "tri_mesh.vert.spv").c_str(), &meshVertex)) {
		std::cout << "error when building the triangle vertex shader module" << std::endl;
	} else {
		std::cout << "red triangle vertex shader successfully loaded" << std::endl;
	}

	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, meshVertex));
	builder._shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragment));
	builder._pipelineLayout = meshPipelineLayout;

	meshPipeline = builder.build_pipeline(device, renderPass);

	//can delete after pipeline creation
	vkDestroyShaderModule(device, fragment, nullptr);
	vkDestroyShaderModule(device, vertex, nullptr);
	vkDestroyShaderModule(device, redFragment, nullptr);
	vkDestroyShaderModule(device, redVertex, nullptr);
	vkDestroyShaderModule(device, meshVertex, nullptr);

	deletionQueue.push_function([=]() {
		vkDestroyPipeline(device, redTrianglePipeline, nullptr);
        vkDestroyPipeline(device, trianglePipeline, nullptr);
		vkDestroyPipeline(device, meshPipeline, nullptr);

		vkDestroyPipelineLayout(device, trianglePipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
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
	triangleMesh.vertices.resize(3);
	triangleMesh.vertices[0].position = {1.f, 1.f, 0.f};
	triangleMesh.vertices[1].position = {-1.f, 1.f, 0.f};
	triangleMesh.vertices[2].position = {0.f, -1.f, 0.f};

	triangleMesh.vertices[0].color = {12.f/255.f, 170.f/255.f, 28.f/255.f};
	triangleMesh.vertices[1].color = {12.f/255.f, 170.f/255.f, 28.f/255.f};
	triangleMesh.vertices[2].color = {12.f/255.f, 170.f/255.f, 28.f/255.f};

	monkeyMesh.load_from_obj("../assets/monkey_smooth.obj");

	upload_mesh(triangleMesh);
	upload_mesh(monkeyMesh);
}

void VulkanEngine::upload_mesh(Mesh& mesh) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = mesh.vertices.size() * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr));
	deletionQueue.push_function([=]() {
		vmaDestroyBuffer(allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
	});

	//copy vertex data to buffer
	void* data;
	vmaMapMemory(allocator, mesh.vertexBuffer.allocation, &data);
	memcpy(data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(allocator, mesh.vertexBuffer.allocation);
}

void VulkanEngine::cleanup() {
	if (_isInitialized) {
		vkWaitForFences(device, 1, &renderFence, VK_TRUE, 1000000000);

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
	//wait for previous gpu instructions to finish
	VK_CHECK(vkWaitForFences(device, 1, &renderFence, VK_TRUE, 1000000000));
	VK_CHECK(vkResetFences(device, 1, &renderFence));

	//grab image from swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000, presentSemaphore, nullptr, &swapchainImageIndex));

	//prepare command buffer for commands
	VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo));

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

	vkCmdBeginRenderPass(commandBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	//draw with graphics pipeline
	VkPipeline* bindedPipeline = pipelineLUT[_selectedShader];
	int vertexCount = 0;

	switch(_selectedShader) {
		case 0:
			vertexCount = 3;
			break;
		case 1:
			vertexCount = 6;
			break;
		case 2:
			vertexCount = monkeyMesh.vertices.size();
			break;
	}

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *bindedPipeline);

	VkDeviceSize offset = 0;
	if (_selectedShader == 2) {
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &monkeyMesh.vertexBuffer.buffer, &offset);
	}
	
	//push constants
	glm::mat4 projection = glm::perspective(glm::radians(45.f), (float) _windowExtent.width / (float) _windowExtent.height, 0.1f, 1000.f);
	glm::mat4 view = glm::lookAt(
		glm::vec3(4 * sin(_frameNumber / 180.f), 0, 4 * cos(_frameNumber / 180.f)),
		glm::vec3(0, 0, 0),
		glm::vec3(0, -1, 0)
	);
	glm::mat4 model = glm::mat4(1.f);

	glm::mat4 meshMatrix = projection * view * model;

	MeshPushConstants constants;
	constants.render_matrix = meshMatrix;

	vkCmdPushConstants(commandBuffer, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

	vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	//submit to queue
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &presentSemaphore;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, renderFence));

	//present queue results
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderSemaphore;
	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

	_frameNumber++;
}

void VulkanEngine::run() {
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit) {
		// Handle events on queue
		while (SDL_PollEvent(&e) != 0) {
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_QUIT) {
				bQuit = true;
			} else if (e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym == SDLK_SPACE) {
					_selectedShader++;
					_selectedShader %= 3;
				}	
			}
		}

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