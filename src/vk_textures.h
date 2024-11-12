#pragma once

#include <vk_types.h>
#include <vk_engine.h>

namespace vkutil {
    bool load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& outImage); 
    bool load_images_from_file(VulkanEngine& engine, const char* files[], AllocatedImage* outImages[], int size);
    void create_empty_image(VulkanEngine& engine, AllocatedImage& outImage, VkExtent2D extent);
    void create_empty_images(VulkanEngine& engine, AllocatedImage* outImages, VkExtent2D extent, int size);
}