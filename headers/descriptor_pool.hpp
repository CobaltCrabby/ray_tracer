#pragma once

#include "render_context.hpp"
#include "vulkan/vulkan_core.h"

class DescriptorPool {
    public:
        VkDescriptorPool pool;
        RenderContext* renderContext;

        DescriptorPool(RenderContext* context, VkDescriptorPoolCreateFlags flags = 0, std::vector<VkDescriptorPoolSize> sizes = {});
        ~DescriptorPool();
};