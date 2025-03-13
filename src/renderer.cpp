#include <cstddef>
#include <iostream>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <renderer.h>
#include <renderpass.h>
#include <pipeline.h>

Renderer::Renderer(RenderContext* context) {
    renderContext = context;

    // create copy command buffer and pool
    VkCommandPoolCreateInfo copyPoolCreateInfo{};
    copyPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    copyPoolCreateInfo.queueFamilyIndex = context->graphicsQueueFamily;
    VK_CHECK(vkCreateCommandPool(context->device, &copyPoolCreateInfo, nullptr, &copyCommandPool));

    VkCommandBufferAllocateInfo copyBufferAllocateInfo{};
    copyBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    copyBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    copyBufferAllocateInfo.commandBufferCount = 1;
    copyBufferAllocateInfo.commandPool = copyCommandPool;
    VK_CHECK(vkAllocateCommandBuffers(context->device, &copyBufferAllocateInfo, &copyCommandBuffer));

    // per frame command pools and buffers
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkCommandPoolCreateInfo poolCreateInfo{};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.queueFamilyIndex = context->graphicsQueueFamily;
        poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // needed to rerecord commands
        VK_CHECK(vkCreateCommandPool(context->device, &poolCreateInfo, nullptr, &frames[i].commandPool));

        VkCommandBufferAllocateInfo bufferAllocateInfo{};
        bufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferAllocateInfo.commandBufferCount = 1;
        bufferAllocateInfo.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(context->device, &bufferAllocateInfo, &frames[i].commandBuffer));
    }

    // create renderpass and framebuffers
    renderPass = new RenderPass(context);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkImageView imageView = renderContext->swapchainImageViews[i];
        Framebuffer* fb = new Framebuffer(renderContext, {imageView}, renderPass); 
        framebuffers.push_back(fb);
    }

    // create and update descriptors
    descriptorPool = new DescriptorPool(renderContext);
    renderImage = new Image(renderContext, &renderContext->allocator, {renderContext->windowExtent.width, renderContext->windowExtent.height, 1}, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
   
    VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VK_CHECK(vkCreateSampler(renderContext->device, &samplerInfo, nullptr, &defaultSampler));

    VkDescriptorImageInfo renderImageInfo;
	renderImageInfo.sampler = defaultSampler;
	renderImageInfo.imageView = renderImage->imageView;
	renderImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::vector<VkWriteDescriptorSet> graphicsWrites;
    VkWriteDescriptorSet graphicsTextureWrite{};
    graphicsTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	graphicsTextureWrite.dstBinding = 0;
	graphicsTextureWrite.descriptorCount = 1;
	graphicsTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    graphicsTextureWrite.pImageInfo = &renderImageInfo;
    graphicsWrites.push_back(graphicsTextureWrite);

    std::vector<VkWriteDescriptorSet> computeWrites;
    VkWriteDescriptorSet computeTextureWrite{};
    computeTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	computeTextureWrite.dstBinding = 0;
	computeTextureWrite.descriptorCount = 1;
	computeTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    computeTextureWrite.pImageInfo = &renderImageInfo;
    computeWrites.push_back(computeTextureWrite);

    std::string bin = std::filesystem::current_path().generic_string() + "/shaders/bin/";
    GraphicsPipeline graphicsPipeline = GraphicsPipeline(renderContext, descriptorPool, renderPass, graphicsWrites, (bin + "raytrace.vert.spv").c_str(), (bin + "raytrace.frag.spv").c_str());
    ComputePipeline computePipeline = ComputePipeline(renderContext, descriptorPool, computeWrites, (bin + "test.comp.spv").c_str());
}

Renderer::~Renderer() {
    vkDestroySampler(renderContext->device, defaultSampler, nullptr);
    vkDestroyCommandPool(renderContext->device, copyCommandPool, nullptr);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        vkDestroyCommandPool(renderContext->device, frames[i].commandPool, nullptr);
        delete framebuffers[i];
    }
    delete descriptorPool;
    delete renderPass;
    delete renderImage;
}

void Renderer::render() {
    std::cout << "test" << std::endl;
}