#pragma once
#define GLM_ENABLE_EXPERIMENTAL

#include "pipeline.hpp"
#include "render_context.hpp"
#include "framebuffer.hpp"
#include "image.hpp"
#include "descriptor_pool.hpp"
#include "imgui.hpp"
#include "render_object.hpp"
#include "buffer_block.hpp"
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>

class Renderer {
    public:
        struct FrameData {
            VkCommandPool commandPool;
            VkCommandBuffer graphicsCmdBuffer;
            VkCommandBuffer logicCmdBuffer;
            VkCommandBuffer materialNewPathCmdBuffer;
            VkCommandBuffer extensionCmdBuffer;
            VkFence computeReady;
            VkFence frameReady;
            VkSemaphore swapImageAvailable, renderFinish, computeFinish;
        };

        struct PathState {
            glm::vec4 origin[1930176]; //xyz = origin, w = uv.x
            glm::vec4 direction[1930176]; //xyz = direction, w = uv.y
            glm::vec4 hitNormal[1930176]; //xyz = normal, w = hit distance
            glm::vec4 throughput[1930176]; //xyz = attenuation, 
        };

        struct IndexQueue {
            uint size = 0;
            uint requests[1930176];
        };

        struct Material {
            glm::vec4 albedo; // w is unused
            glm::vec4 emission; // xyz is color, w is strength
        };

        struct MaterialPushConstants {
            uint dispatchCount = 0;
        };

        RenderContext* renderContext;
        DescriptorPool* descriptorPool;
        RenderPass* renderPass;
        ImGuiContext* imguiContext;
        Image* renderImages[RenderContext::RenderContext::FRAMES_IN_FLIGHT];

        BufferBlock* wavefrontBlock;
        BufferBlock::SubBuffer pathStateBuffer;
        BufferBlock::SubBuffer newPathQueue;
        BufferBlock::SubBuffer extensionRayQueue;
        BufferBlock::SubBuffer materialRequestQueue;
        
        BufferBlock* geometryBlock;
        BufferBlock::SubBuffer vertexBuffer;
        BufferBlock::SubBuffer triangleBuffer;
        BufferBlock::SubBuffer bvhBuffer;
        BufferBlock::SubBuffer objectBuffer;
        BufferBlock::SubBuffer materialBuffer;

        GraphicsPipeline* graphicsPipeline;
        ComputePipeline* computePipeline;
        ComputePipeline* logicPipeline;
        ComputePipeline* newPathPipeline;
        ComputePipeline* materialPipeline;
        ComputePipeline* extensionPipeline;

        FrameData frames[RenderContext::FRAMES_IN_FLIGHT];
        VkCommandPool copyCommandPool;
        VkCommandBuffer copyCommandBuffer;
        VkFence copyFence;
        VkSampler defaultSampler;

        std::vector<Framebuffer*> framebuffers;
        std::vector<Material> materials;
        ImGuiStats renderStats{};
        uint frameNumber;
        uint dispatchCount = 0;

        explicit Renderer(RenderContext* context);
        ~Renderer();

        void render();
        void addWrite(std::vector<VkWriteDescriptorSet>& sets, VkWriteDescriptorSet newSet, uint binding);
};