#include <SDL_video.h>
#include <SDL_vulkan.h>
#include <SDL.h>
#include <vulkan/vulkan_core.h>
#include <render_context.hpp>
#include <VkBootstrap.h>

// only define first time use
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

RenderContext::RenderContext() {
    // create the vulkan instance
    vkb::InstanceBuilder instBuilder;
    auto build = instBuilder.set_app_name("path tracer")
        .request_validation_layers(true)
        .require_api_version(1, 1, 0)
        .use_default_debug_messenger()
        .enable_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) // need for mac
        .build();

    vkb::Instance inst = build.value();
    instance = inst.instance;
    debugMessenger = inst.debug_messenger;

    // create window
    SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");

    window = SDL_CreateWindow("path tracer", 
        SDL_WINDOWPOS_UNDEFINED, 
        SDL_WINDOWPOS_UNDEFINED, 
        windowExtent.width, 
        windowExtent.height, 
        window_flags);
    SDL_Vulkan_CreateSurface(window, instance, &surface);

    // pick GPU
    vkb::PhysicalDeviceSelector selector{inst};
    vkb::PhysicalDevice vkbPhysicalDevice = selector.set_minimum_version(1, 1)
        .set_surface(surface)
        .select()
        .value();

    physicalDevice = vkbPhysicalDevice.physical_device;

    // create logical device
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
	VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features{};
	shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETER_FEATURES; // dont think i actually need this but whatever
	shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&shader_draw_parameters_features)
        .build()
        .value();
    device = vkbDevice.device;

    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    // my device has no dedicated compute queue so use graphics queue which always supports compute
    computeQueue = graphicsQueue;
    computeQueueFamily = graphicsQueueFamily;

    // create vma allocator
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.device = device;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.instance = instance;
    vmaCreateAllocator(&allocatorInfo, &allocator);

    // create swapchain
    VkSurfaceFormatKHR surfaceFormat;
    surfaceFormat.format = VK_FORMAT_R8G8B8A8_SRGB;
    surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};
    auto vkbSwapchain = swapchainBuilder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(windowExtent.width, windowExtent.height)
        .set_required_min_image_count(vkb::SwapchainBuilder::BufferMode::DOUBLE_BUFFERING)
        .set_desired_format(surfaceFormat)
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        .build()
        .value();

    swapchain = vkbSwapchain.swapchain;
    swapchainFormat = vkbSwapchain.image_format;
    swapchainImageViews = vkbSwapchain.get_image_views().value();
}

RenderContext::~RenderContext() {
    vmaDestroyAllocator(allocator);
    for (int i = 0; i < swapchainImageViews.size(); i++) {
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkb::destroy_debug_utils_messenger(instance, debugMessenger, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
}