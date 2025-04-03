#include "vulkan/vulkan_core.h"
#include "pipeline.hpp"
#include <cstddef>
#include <render_context.hpp>
#include <vk_mem_alloc.h>
#include <image.hpp>

void Image::transitionLayout(VkImageLayout layout, SubmitInfo submit) {
    VkCommandBuffer cmd = submit.submitBuffer;
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageMemoryBarrier imageBarrier = {};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = layout;
    imageBarrier.image = image;
    imageBarrier.subresourceRange = range;
    imageBarrier.srcAccessMask = 0;
    imageBarrier.dstAccessMask = 0;

    //barrier the image into the transfer-receive layout
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

	VK_CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;

	VK_CHECK(vkQueueSubmit(submit.queue, 1, &submitInfo, *submit.submitFence));
	vkWaitForFences(renderContext->device, 1, submit.submitFence, VK_TRUE, 9999999999);
	vkResetFences(renderContext->device, 1, submit.submitFence);

	vkResetCommandPool(renderContext->device, submit.submitPool, 0);
}

Image::Image(RenderContext* context, VmaAllocator* allocator, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage) {
    renderContext = context;
    this->allocator = allocator;

    VkImageCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	createInfo.imageType = VK_IMAGE_TYPE_2D;
	createInfo.format = format;
	createInfo.extent = extent;
	createInfo.mipLevels = 1;
	createInfo.arrayLayers = 1;
	createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	createInfo.usage = usage;

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateImage(*allocator, &createInfo, &imgAllocInfo, &image, &allocation, nullptr));

    VkImageViewCreateInfo viewCreateInfo{};
    viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreateInfo.image = image;
    viewCreateInfo.format = format;
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.components = VkComponentMapping{
        VK_COMPONENT_SWIZZLE_IDENTITY,  // R
        VK_COMPONENT_SWIZZLE_IDENTITY,  // G
        VK_COMPONENT_SWIZZLE_IDENTITY,  // B
        VK_COMPONENT_SWIZZLE_IDENTITY   // A
    };
    viewCreateInfo.subresourceRange = VkImageSubresourceRange{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, 1,   // 0th mip level, 1 level
        0, 1    // 0th array layer, 1 layer
    };

    VK_CHECK(vkCreateImageView(context->device, &viewCreateInfo, nullptr, &imageView));
}

Image::~Image() {
    vmaDestroyImage(*allocator, image, allocation);
    vkDestroyImageView(renderContext->device, imageView, nullptr);
}