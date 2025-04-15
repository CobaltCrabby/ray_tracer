#pragma once

#include "pipeline.hpp"
#include "render_context.hpp"
#include "framebuffer.hpp"
#include "image.hpp"
#include "descriptor_pool.hpp"
#include "imgui.hpp"
#include "render_object.hpp"
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

        struct PathState {
            glm::vec4 origin[1930176];
            glm::vec4 direction[1930176];
        };

        struct IndexQueue {
            uint size = 0;
            uint requests[1930176];
        };

        RenderContext* renderContext;
        DescriptorPool* descriptorPool;
        RenderPass* renderPass;
        ImGuiContext* imguiContext;
        Image* renderImages[RenderContext::RenderContext::FRAMES_IN_FLIGHT];

        Buffer* pathStateBuffer;
        Buffer* newPathQueue;
        Buffer* extensionRayQueue;
        Buffer* vertexBuffer;
        Buffer* triangleBuffer;
        Buffer* BVHBuffer;

        GraphicsPipeline* graphicsPipeline;
        ComputePipeline* computePipeline;
        ComputePipeline* newPathPipeline;
        ComputePipeline* extensionPipeline;

        FrameData frames[RenderContext::FRAMES_IN_FLIGHT];
        VkCommandPool copyCommandPool;
        VkCommandBuffer copyCommandBuffer;
        VkFence copyFence;
        VkSampler defaultSampler;

        std::vector<Framebuffer*> framebuffers;
        ImGuiStats renderStats{};
        uint frameNumber;

        explicit Renderer(RenderContext* context);
        ~Renderer();

        void render();
        void addWrite(std::vector<VkWriteDescriptorSet>& sets, VkWriteDescriptorSet newSet, uint binding);
};