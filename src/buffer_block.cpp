#include "buffer_block.hpp"

BufferBlock::BufferBlock(VkDevice device, VkCommandPool commandPool, VkCommandBuffer cmdBuffer, VkFence* fence, VkQueue queue, VmaAllocator alloc, VkBufferUsageFlags flags) {
    this->device = device;
    this->cmdPool = commandPool;
    this->cmdBuffer = cmdBuffer;
    this->fence = fence;
    this->queue = queue;
    this->allocator = alloc;
    this->usageFlags = flags;
}

BufferBlock::~BufferBlock() {
    vmaClearVirtualBlock(virtualBlock);
    vmaDestroyVirtualBlock(virtualBlock);
    delete buffer;
}

void BufferBlock::addSubBuffer(uint size) {
    subBufferInfo.push_back(std::make_tuple(size, nullptr));
}

void BufferBlock::addSubBuffer(uint size, void* data) {
    subBufferInfo.push_back(std::make_tuple(size, data));
}

std::vector<BufferBlock::SubBuffer> BufferBlock::allocateBlock() {
    std::vector<BufferBlock::SubBuffer> subBuffers;

    uint totalSize = 0;
    bool copy = false;
    for (auto& info : subBufferInfo) {
        totalSize += std::get<0>(info);
        totalSize += 16 - (std::get<0>(info) % 16);
        if (std::get<1>(info) != nullptr) copy = true;
    }

    // creates actual buffer in gpu memory
    buffer = new Buffer(allocator, totalSize, copy ? usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT : usageFlags);

    VmaVirtualBlockCreateInfo virtualInfo{};
    virtualInfo.size = totalSize;
    VK_CHECK(vmaCreateVirtualBlock(&virtualInfo, &virtualBlock));

    // allocate virtual blocks
    for (auto& info : subBufferInfo) {
        VmaVirtualAllocationCreateInfo allocInfo{};
        allocInfo.alignment = 16;
        allocInfo.size = std::get<0>(info);
        void* data = std::get<1>(info);

        VmaVirtualAllocation alloc;
        VkDeviceSize offset;
        VK_CHECK(vmaVirtualAllocate(virtualBlock, &allocInfo, &alloc, &offset));

        subBuffers.push_back({buffer, alloc, offset, allocInfo.size});
        if (data != nullptr) { // unoptimal but whateverrrrr
            // it crashes here LOL
            buffer->copyBuffer(device, cmdPool, cmdBuffer, fence, queue, allocator, allocInfo.size, totalSize, usageFlags, data, offset);
        }
    }

    return subBuffers;
}