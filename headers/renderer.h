#pragma once

#include "render_context.h"
#include "framebuffer.h"
#include "image.h"
#include "descriptor_pool.h"
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

class Renderer {
    public:
        const static unsigned int FRAMES_IN_FLIGHT = 2;

        RenderContext* renderContext;
        DescriptorPool* descriptorPool;
        RenderPass* renderPass;
        Image* renderImage;

        FrameData frames[FRAMES_IN_FLIGHT];
        VkCommandPool copyCommandPool;
        VkCommandBuffer copyCommandBuffer;
        VkSampler defaultSampler;

        std::vector<Framebuffer*> framebuffers;

        Renderer(RenderContext* context);
        ~Renderer();
        void render();
};