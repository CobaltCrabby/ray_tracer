#pragma once

#include "pipeline.h"
#include "render_context.h"
#include "framebuffer.h"
#include "image.h"
#include "descriptor_pool.h"
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>

class Renderer {
    public:
        struct FrameData {
            VkCommandPool commandPool;
            VkCommandBuffer commandBuffer;
            VkFence frameReady;
            VkSemaphore swapImageAvailable, renderFinish;
        };

        const static unsigned int FRAMES_IN_FLIGHT = 2;

        RenderContext* renderContext;
        DescriptorPool* descriptorPool;
        RenderPass* renderPass;
        Image* renderImage;
        GraphicsPipeline* graphicsPipeline;
        ComputePipeline* computePipeline;

        FrameData frames[FRAMES_IN_FLIGHT];
        VkCommandPool copyCommandPool;
        VkCommandBuffer copyCommandBuffer;
        VkFence copyFence;
        VkSampler defaultSampler;

        std::vector<Framebuffer*> framebuffers;
        uint frameNumber;

        Renderer(RenderContext* context);
        ~Renderer();
        void render();
};