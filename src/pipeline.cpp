#include "vulkan/vulkan_core.h"
#include <cstddef>
#include <pipeline.hpp>
#include <string>
#include <vector>

void GraphicsPipeline::updateDescriptors(std::vector<VkWriteDescriptorSet> writes, VkDescriptorSet descriptorSet) {
    for (int i = 0; i < writes.size(); i++) {
        writes[i].dstSet = descriptorSet;
    }

    vkUpdateDescriptorSets(renderContext->device, writes.size(), writes.data(), 0, nullptr);
}

void GraphicsPipeline::generateScreenQuad(VkCommandPool submitPool, VkCommandBuffer submitBuffer, VkFence* submitFence, VkQueue queue, VmaAllocator allocator) {
    std::vector<Vertex> vertices = {
        {{1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}}
    };

    std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
    
    vertexBuffer = new Buffer(renderContext->device, submitPool, submitBuffer, submitFence, queue, allocator, sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
    indexBuffer = new Buffer(renderContext->device, submitPool, submitBuffer, submitFence, queue, allocator, sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data());
}

GraphicsPipeline::GraphicsPipeline(RenderContext* context, DescriptorPool* pool, RenderPass* pass, std::vector<VkWriteDescriptorSet> fragmentWrites, SubmitInfo* submitInfo, const char* vertexShaderPath, const char* fragmentShaderPath) {
    renderContext = context;
    descriptorPool = pool;
    descriptorCount = fragmentWrites.size();
    
    // vertex input
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

    // viewport
    VkViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.height = (float) renderContext->windowExtent.height;
    viewport.width = (float) renderContext->windowExtent.width;
    viewport.minDepth = 1.f; // reversed depth buffer
    viewport.maxDepth = 0.f;

    VkRect2D scissor; // full window
    scissor.extent = renderContext->windowExtent;
    scissor.offset = {0, 0};

    // rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.lineWidth = 1.f;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f;
	rasterizer.depthBiasClamp = 0.0f;
	rasterizer.depthBiasSlopeFactor = 0.0f;

    // multisampling (not using)
    VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling.minSampleShading = 1.f;
	multisampling.pSampleMask = nullptr;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

    // color attachment
    VkPipelineColorBlendAttachmentState colorAttachment{};
	colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorAttachment.blendEnable = VK_FALSE;

    // depth stencil (not using)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // vertex description
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	bindings.push_back(mainBinding);

	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 1;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	attributes.push_back(positionAttribute);
	attributes.push_back(uvAttribute);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = bindings.size();
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
	vertexInputInfo.vertexAttributeDescriptionCount = attributes.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    // create bindings and set layout
    std::vector<VkDescriptorSetLayoutBinding> setBindings;
    for (const auto& write : fragmentWrites) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = write.dstBinding;
        binding.descriptorCount = 1;
        binding.descriptorType = write.descriptorType;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        
        setBindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo{};
    setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutCreateInfo.bindingCount = fragmentWrites.size();
    setLayoutCreateInfo.pBindings = setBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(context->device, &setLayoutCreateInfo, nullptr, &setLayout));

    // create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pSetLayouts = &setLayout;
	layoutInfo.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(context->device, &layoutInfo, nullptr, &pipelineLayout));

    // load shaders
    VkShaderModule fragmentShader;
    VkShaderModule vertexShader;
	if (!load_shader_module(context, fragmentShaderPath, &fragmentShader)) {
		std::cout << "error loading fragment shader: " << fragmentShaderPath << std::endl;
	}

    if (!load_shader_module(context, vertexShaderPath, &vertexShader)) {
		std::cout << "error loading vertex shader: " << vertexShaderPath << std::endl;
	}

    VkPipelineShaderStageCreateInfo fragmentStageInfo{};
	fragmentStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragmentStageInfo.pName = "main"; // entry point for shader
	fragmentStageInfo.module = fragmentShader;

    VkPipelineShaderStageCreateInfo vertexStageInfo{};
	vertexStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertexStageInfo.pName = "main"; // entry point for shader
	vertexStageInfo.module = vertexShader;

    VkPipelineShaderStageCreateInfo shaderStages[] = {fragmentStageInfo, vertexStageInfo};

    // pieline creation
    VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorAttachment;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = pass->renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	//it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(renderContext->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		std::cout << "failed to create pipeline" << std::endl;;
	} else {
		pipeline = newPipeline;
	}

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool->pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &setLayout;

    VK_CHECK(vkAllocateDescriptorSets(context->device, &allocateInfo, &descriptorSet));

    generateScreenQuad(submitInfo->submitPool, submitInfo->submitBuffer, submitInfo->submitFence, submitInfo->queue, submitInfo->allocator);
    updateDescriptors(fragmentWrites, descriptorSet);
    
    vkDestroyShaderModule(context->device, fragmentShader, nullptr);
    vkDestroyShaderModule(context->device, vertexShader, nullptr);
}

GraphicsPipeline::~GraphicsPipeline() {
    delete vertexBuffer;
    delete indexBuffer;
    vkDestroyDescriptorSetLayout(renderContext->device, setLayout, nullptr);
    vkDestroyPipelineLayout(renderContext->device, pipelineLayout, nullptr);
    vkDestroyPipeline(renderContext->device, pipeline, nullptr);
}

void ComputePipeline::updateDescriptors(std::vector<VkWriteDescriptorSet> writes, VkDescriptorSet set) {
    for (int i = 0; i < writes.size(); i++) {
        writes[i].dstSet = descriptorSet;
    }

    vkUpdateDescriptorSets(renderContext->device, writes.size(), writes.data(), 0, nullptr);
}

ComputePipeline::ComputePipeline(RenderContext* context, DescriptorPool* pool, std::vector<VkWriteDescriptorSet> writes, const char* shaderFilePath) {
    renderContext = context;
    descriptorPool = pool;

    // create bindings and set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& write : writes) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = write.dstBinding;
        binding.descriptorCount = 1;
        binding.descriptorType = write.descriptorType;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo{};
    setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutCreateInfo.bindingCount = bindings.size();
    setLayoutCreateInfo.pBindings = bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(context->device, &setLayoutCreateInfo, nullptr, &setLayout));

    // create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pSetLayouts = &setLayout;
	layoutInfo.setLayoutCount = 1;
    // push constants in the future

    VK_CHECK(vkCreatePipelineLayout(context->device, &layoutInfo, nullptr, &pipelineLayout));

    // load shader
    VkShaderModule computeShader;
	if (!load_shader_module(context, shaderFilePath, &computeShader)) {
		std::cout << "error loading compute shader: " << shaderFilePath << std::endl;
	}

    VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.pName = "main"; // entry point for shader
	stageInfo.module = computeShader;

    // create pipeline
    VkComputePipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(context->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline));

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool->pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &setLayout;

    VK_CHECK(vkAllocateDescriptorSets(context->device, &allocateInfo, &descriptorSet));

    updateDescriptors(writes, descriptorSet);

    vkDestroyShaderModule(context->device, computeShader, nullptr);
}

ComputePipeline::~ComputePipeline() {
    vkDestroyDescriptorSetLayout(renderContext->device, setLayout, nullptr);
    vkDestroyPipelineLayout(renderContext->device, pipelineLayout, nullptr);
    vkDestroyPipeline(renderContext->device, pipeline, nullptr);
}