#pragma once

#include "render_context.hpp"
#include "vulkan/vulkan_core.h"

class Buffer {
    public:
        VkBuffer buffer;

        VkDevice* device;
        VmaAllocation allocation;
        VmaAllocator allocator;
        VmaAllocationInfo allocInfo;

        Buffer() = default;
        Buffer(VmaAllocator alloc, size_t bufferSize, VkBufferUsageFlags flags);
        Buffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer cmdBuffer, VkFence* fence, VkQueue queue, VmaAllocator alloc, size_t bufferSize, VkBufferUsageFlags flags, void* bufferData);
        ~Buffer();
};