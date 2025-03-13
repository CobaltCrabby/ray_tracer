#pragma once

#include "render_context.h"
#include <renderpass.h>
#include <vulkan/vulkan_core.h>

class Framebuffer {
    public:
        VkFramebuffer framebuffer;
        RenderContext* renderContext;
        Framebuffer(RenderContext* renderContext, std::vector<VkImageView> attachment, RenderPass* renderPass);
        ~Framebuffer();
};