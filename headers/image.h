#pragma once

#include <vk_mem_alloc.h>
#include <render_context.h>
#include "pipeline.h"
#include "render_context.h"
#include "vulkan/vulkan_core.h"

class Image {
    public:
        RenderContext* renderContext;
        VkImage image;
        VkImageView imageView;
        VmaAllocation allocation;
        VmaAllocator* allocator;

        void transitionLayout(VkImageLayout layout, SubmitInfo submit);
        Image(RenderContext* context, VmaAllocator* allocator, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage);
        ~Image();
};