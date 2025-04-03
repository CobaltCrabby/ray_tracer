#pragma once

#include "render_context.hpp"
#include "renderpass.hpp"
#include <vulkan/vulkan_core.h>

class Framebuffer {
    public:
        VkFramebuffer framebuffer;
        RenderContext* renderContext;
        Framebuffer(RenderContext* renderContext, std::vector<VkImageView> attachment, RenderPass* renderPass);
        ~Framebuffer();
};