#include "vulkan/vulkan_core.h"
#include <buffer.hpp>

Buffer::Buffer(VmaAllocator alloc, size_t bufferSize, VkBufferUsageFlags flags) {
	allocator = alloc;

	// allocate buffer
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bufferSize;
	bufferInfo.usage = flags;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &buffer, &allocation, nullptr));
}

Buffer::Buffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer cmdBuffer, VkFence* fence, VkQueue queue, VmaAllocator alloc, size_t bufferSize, VkBufferUsageFlags flags, void* bufferData) {
    VkBufferCreateInfo stagingInfo{};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = bufferSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	// create staging buffer
	VkBuffer stagingBuffer;
	VmaAllocation stagingAllocation;
	allocator = alloc;

	VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &vmaAllocInfo, &stagingBuffer, &stagingAllocation, nullptr));

	// copy data to staging buffer
	void* data;
	vmaMapMemory(allocator, stagingAllocation, &data);
	memcpy(data, bufferData, bufferSize);
	vmaUnmapMemory(allocator, stagingAllocation);

	// allocate buffer
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bufferSize;
	bufferInfo.usage = flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	//vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &buffer, &allocation, &allocInfo));

	// copy buffer
    VkCommandBuffer cmd = cmdBuffer;
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

	VkBufferCopy copy;
	copy.size = bufferSize;
	copy.srcOffset = 0;
	copy.dstOffset = 0;
	vkCmdCopyBuffer(cmd, stagingBuffer, buffer, 1, &copy);
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

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, *fence));
	vkWaitForFences(device, 1, fence, VK_TRUE, 9999999999);
	vkResetFences(device, 1, fence);

	vkResetCommandPool(device, commandPool, 0);
	vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

Buffer::~Buffer() {
	// seg faulting here, changed when i made the pipelines pointers, check memory adresses	
	vmaDestroyBuffer(allocator, buffer, allocation);
}