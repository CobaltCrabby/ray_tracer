#pragma once

#include "render_context.h"
#include "vulkan/vulkan_core.h"

class DescriptorPool {
    public:
        VkDescriptorPool pool;
        RenderContext* renderContext;

        DescriptorPool(RenderContext* context);
        ~DescriptorPool();
};