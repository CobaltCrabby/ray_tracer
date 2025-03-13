#include "vulkan/vulkan_core.h"
#include <cstddef>
#include <render_context.h>
#include <vk_mem_alloc.h>
#include <image.h>

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