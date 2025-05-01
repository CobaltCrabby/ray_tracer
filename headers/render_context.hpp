#pragma once

#include "SDL_vulkan.h"
#include "vulkan/vulkan_core.h"
#include <vk_mem_alloc.h>
#include <vector>
#include <iostream>

class RenderContext {
    #define VK_CHECK(x)\
	do {\
		VkResult err = x;\
		if (err) {\
			std::cout << "Error detected: " << err << std::endl;\
			abort();\
		}\
	} while (0)

    public:        
        const VkExtent2D windowExtent = {1728, 1117};
        const static unsigned int FRAMES_IN_FLIGHT = 2;
        const static unsigned int QUERY_SIZE = 3;

        // vulkan context objects
        VkInstance instance;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkSwapchainKHR swapchain;
        std::vector<VkImageView> swapchainImageViews;

        VkFormat swapchainFormat;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkQueryPool queryPool;
        VmaAllocator allocator;

        // queues
        VkQueue graphicsQueue;
        uint graphicsQueueFamily;
        VkQueue computeQueue;
        uint computeQueueFamily;

        // window managing
        struct SDL_Window* window = {nullptr};

        // functions
        RenderContext();
        ~RenderContext();

};