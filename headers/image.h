#pragma once

#include <vk_mem_alloc.h>
#include <render_context.h>
#include "render_context.h"
#include "vulkan/vulkan_core.h"

class Image {
    public:
        RenderContext* renderContext;
        VkImage image;
        VkImageView imageView;
        VmaAllocation allocation;
        VmaAllocator* allocator;

        Image(RenderContext* context, VmaAllocator* allocator, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage);
        ~Image();
};