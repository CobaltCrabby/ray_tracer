#pragma once

#include "render_context.hpp"
#include <vulkan/vulkan_core.h>

class RenderPass {
    public:
        VkRenderPass renderPass;
        RenderContext* renderContext;

        RenderPass(RenderContext* context);
        ~RenderPass();
};