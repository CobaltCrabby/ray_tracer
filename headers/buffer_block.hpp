#pragma once

#include "buffer.hpp"
#include "render_context.hpp"

class BufferBlock {
    public:
        struct SubBuffer {
            Buffer* block;
            VmaVirtualAllocation alloc;
            VkDeviceSize offset;
            VkDeviceSize size;
        };

        // for staging buffer
        VkDevice device;
        VkCommandPool cmdPool;
        VkCommandBuffer cmdBuffer;
        VkFence* fence;
        VkQueue queue;

        Buffer* buffer;
        VmaVirtualBlock virtualBlock;

        VmaAllocator allocator;
        VkBufferUsageFlags usageFlags;
        std::vector<std::tuple<uint, void*>> subBufferInfo; // size, data (optional)

        BufferBlock(VkDevice device, VkCommandPool commandPool, VkCommandBuffer cmdBuffer, VkFence* fence, VkQueue queue, VmaAllocator alloc, VkBufferUsageFlags flags);
        ~BufferBlock();
        void addSubBuffer(uint size);
        void addSubBuffer(uint size, void* data);
        std::vector<SubBuffer> allocateBlock();
};