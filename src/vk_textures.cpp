#include <iostream>
#include <vk_textures.h>
#include <vk_initializers.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

bool vkutil::load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& outImage) {
    int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        std::cout << "failed to load texture" << std::endl;
        return false;
    }

    void* pixel_ptr = pixels;
    VkDeviceSize size = texWidth * texHeight * 4;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
    AllocatedBuffer stagingBuffer = engine.create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data;
    vmaMapMemory(engine.allocator, stagingBuffer.allocation, &data);
    memcpy(data, pixel_ptr, static_cast<size_t>(size));
    vmaUnmapMemory(engine.allocator, stagingBuffer.allocation);
    stbi_image_free(pixels);

    VkExtent3D imageExtent;
    imageExtent.width = texWidth;
    imageExtent.height = texHeight;
    imageExtent.depth = 1;

    VkImageCreateInfo imageInfo = vkinit::imageCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, imageExtent);
    
    AllocatedImage image;
    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(engine.allocator, &imageInfo, &imgAllocInfo, &image.image, &image.allocation, nullptr);

    engine.immediate_submit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		VkImageMemoryBarrier imageBarrier_toTransfer = {};
		imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

		imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier_toTransfer.image = image.image;
		imageBarrier_toTransfer.subresourceRange = range;

		imageBarrier_toTransfer.srcAccessMask = 0;
		imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		//barrier the image into the transfer-receive layout
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imageExtent;

        //copy the buffer into the image
        vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier imageBarrier_toReadable = {};
		imageBarrier_toReadable.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

		imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrier_toReadable.image = image.image;
		imageBarrier_toReadable.subresourceRange = range;

		imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        //barrier the image into the shader readable layout
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toReadable);
    });

    engine.deletionQueue.push_function([=]() {
        vmaDestroyImage(engine.allocator, image.image, image.allocation);
    });

    vmaDestroyBuffer(engine.allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    std::cout << "texture loaded successfully" << std::endl;
    outImage = image;

    return true;
}

void vkutil::create_empty_image(VulkanEngine& engine, AllocatedImage& outImage, VkExtent2D extent) {
    VkDeviceSize size = extent.width * extent.height * 4;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
    AllocatedBuffer stagingBuffer = engine.create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    VkExtent3D imageExtent;
    imageExtent.width = extent.width;
    imageExtent.height = extent.height;
    imageExtent.depth = 1;

    VkImageCreateInfo imageInfo = vkinit::imageCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, imageExtent);
    
    AllocatedImage image;
    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(engine.allocator, &imageInfo, &imgAllocInfo, &image.image, &image.allocation, nullptr);

    engine.immediate_submit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

        //ignore the name
		VkImageMemoryBarrier imageBarrier_toTransfer = {};
		imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

		imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageBarrier_toTransfer.image = image.image;
		imageBarrier_toTransfer.subresourceRange = range;

		imageBarrier_toTransfer.srcAccessMask = 0;
		imageBarrier_toTransfer.dstAccessMask = 0;

		//barrier the image into the transfer-receive layout
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);
    });

    engine.deletionQueue.push_function([=]() {
        vmaDestroyImage(engine.allocator, image.image, image.allocation);
    });

    vmaDestroyBuffer(engine.allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    std::cout << "image created successfully" << std::endl;
    outImage = image;
}