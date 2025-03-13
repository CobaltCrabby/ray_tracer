#pragma once

#include "render_context.h"
#include "renderer.h"
#include "vulkan/vulkan_core.h"
#include <descriptor_pool.h>

#include <fstream>

static bool load_shader_module(RenderContext* context, const char* filePath, VkShaderModule* outShaderModule) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        std::cout << "cannot find file " << filePath << std::endl;
        return false;
    }

    //read file into a buffer
    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(context->device, &createInfo, nullptr, &module));
    *outShaderModule = module;
    return true;
}

class GraphicsPipeline {
    public:
        struct Vertex {
            glm::vec3 position;
            glm::vec2 uv;
        };

        RenderContext* renderContext;
        DescriptorPool* descriptorPool;
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSet descriptorSet;
        VkDescriptorSetLayout setLayout;

        void updateDescriptors(std::vector<VkWriteDescriptorSet> writes, VkDescriptorSet set);

        GraphicsPipeline(RenderContext* context, DescriptorPool* pool, RenderPass* pass, std::vector<VkWriteDescriptorSet> fragmentWrites, const char* vertexShaderPath, const char* fragmentShaderPath);
        ~GraphicsPipeline();
};

class ComputePipeline {
    public:
        RenderContext* renderContext;
        DescriptorPool* descriptorPool;
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSet descriptorSet;
        VkDescriptorSetLayout setLayout;
        
        void updateDescriptors(std::vector<VkWriteDescriptorSet> writes, VkDescriptorSet set);

        ComputePipeline(RenderContext* context, DescriptorPool* pool, std::vector<VkWriteDescriptorSet> writes, const char* shaderFilePath);
        ~ComputePipeline();
};