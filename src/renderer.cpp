#include <cstddef>
#include <cstdio>
#include <iostream>
#include <vector>
#include <renderer.hpp>

Renderer::Renderer(RenderContext* context) {
    renderContext = context;

    // create copy command buffer and pool
    VkCommandPoolCreateInfo copyPoolCreateInfo{};
    copyPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    copyPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // needed to rerecord commands
    copyPoolCreateInfo.queueFamilyIndex = context->graphicsQueueFamily;
    VK_CHECK(vkCreateCommandPool(context->device, &copyPoolCreateInfo, nullptr, &copyCommandPool));

    VkCommandBufferAllocateInfo copyBufferAllocateInfo{};
    copyBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    copyBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    copyBufferAllocateInfo.commandBufferCount = 1;
    copyBufferAllocateInfo.commandPool = copyCommandPool;
    VK_CHECK(vkAllocateCommandBuffers(context->device, &copyBufferAllocateInfo, &copyCommandBuffer));

    VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    //fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(renderContext->device, &fenceInfo, nullptr, &copyFence));

    // per frame command pools and buffers
    for (int i = 0; i < RenderContext::FRAMES_IN_FLIGHT; i++) {
        VkCommandPoolCreateInfo poolCreateInfo{};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.queueFamilyIndex = context->graphicsQueueFamily;
        poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // needed to rerecord commands
        VK_CHECK(vkCreateCommandPool(context->device, &poolCreateInfo, nullptr, &frames[i].commandPool));

        VkCommandBuffer allocatedCmdBuffers[] = {frames[i].logicCmdBuffer, frames[i].materialNewPathCmdBuffer, frames[i].extensionCmdBuffer, frames[i].graphicsCmdBuffer};
        VkCommandBufferAllocateInfo bufferAllocateInfo{};
        bufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferAllocateInfo.commandBufferCount = 4;
        bufferAllocateInfo.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(context->device, &bufferAllocateInfo, allocatedCmdBuffers));

        // ??? idk pointer reference stuff
        frames[i].logicCmdBuffer = allocatedCmdBuffers[0];
        frames[i].materialNewPathCmdBuffer = allocatedCmdBuffers[1];
        frames[i].extensionCmdBuffer = allocatedCmdBuffers[2];
        frames[i].graphicsCmdBuffer = allocatedCmdBuffers[3];

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(renderContext->device, &fenceInfo, nullptr, &frames[i].frameReady));
        VK_CHECK(vkCreateFence(renderContext->device, &fenceInfo, nullptr, &frames[i].computeReady));

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(renderContext->device, &semaphoreInfo, nullptr, &frames[i].renderFinish));
        VK_CHECK(vkCreateSemaphore(renderContext->device, &semaphoreInfo, nullptr, &frames[i].swapImageAvailable));
        VK_CHECK(vkCreateSemaphore(renderContext->device, &semaphoreInfo, nullptr, &frames[i].computeFinish));
    }

    // create renderpass and framebuffers
    renderPass = new RenderPass(context);
    for (int i = 0; i < RenderContext::FRAMES_IN_FLIGHT; i++) {
        VkImageView imageView = renderContext->swapchainImageViews[i];
        Framebuffer* fb = new Framebuffer(renderContext, {imageView}, renderPass); 
        framebuffers.push_back(fb);
    }

    SubmitInfo submitInfo{};
    submitInfo.submitBuffer = copyCommandBuffer;
    submitInfo.submitFence = &copyFence;
    submitInfo.submitPool = copyCommandPool;
    submitInfo.queue = renderContext->graphicsQueue;
    submitInfo.allocator = renderContext->allocator;

    // make read_obj part of BLAS so you can directly write to tri buffer
    Material red{};
    red.albedo = glm::vec4(1.f, 0.f, 0.f, 0.f);
    red.emission = glm::vec4(0.f);

    Material whiteLight{};
    whiteLight.albedo = glm::vec4(0.f, 1.f, 0.f, 1.f);
    whiteLight.emission = glm::vec4(1.f, 1.f, 1.f, 5.f);

    materials.push_back(red);
    materials.push_back(whiteLight);

    BLAS blas;
    blas.readObj("assets/rb.obj");
    blas.createRenderObject("assets/rb.obj", 0, glm::vec3(-0.5f, 0.f, 0.f), glm::vec3(0.f, 135.f, 0.f), glm::vec3(1.f));
    blas.createRenderObject("assets/rb.obj", 1, glm::vec3(0.5f, 0.f, 0.f), glm::vec3(0.f, 45.f, 0.f), glm::vec3(1.f));

    // create suballocated buffers
    descriptorPool = new DescriptorPool(renderContext);

    geometryBlock = new BufferBlock(renderContext->device, copyCommandPool, copyCommandBuffer, &copyFence, renderContext->graphicsQueue, renderContext->allocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    geometryBlock->addSubBuffer(sizeof(BLAS::Vertex) * blas.vertices.size(), (void*) blas.vertices.data());
    geometryBlock->addSubBuffer(sizeof(BLAS::Triangle) * blas.triangles.size(), (void*) blas.triangles.data());
    geometryBlock->addSubBuffer(sizeof(BLAS::BVHNode) * blas.bvhNodes.size(), (void*) blas.bvhNodes.data());
    geometryBlock->addSubBuffer(sizeof(BLAS::RenderObject) * blas.renderObjects.size(), (void*) blas.renderObjects.data());
    geometryBlock->addSubBuffer(sizeof(Material) * materials.size(), (void*) materials.data());
    std::vector<BufferBlock::SubBuffer> geometryBuffers = geometryBlock->allocateBlock();


    vertexBuffer = geometryBuffers[0];
    triangleBuffer = geometryBuffers[1];
    bvhBuffer = geometryBuffers[2];
    objectBuffer = geometryBuffers[3];
    materialBuffer = geometryBuffers[4];

    wavefrontBlock = new BufferBlock(renderContext->device, copyCommandPool, copyCommandBuffer, &copyFence, renderContext->graphicsQueue, renderContext->allocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    wavefrontBlock->addSubBuffer(sizeof(PathState));
    wavefrontBlock->addSubBuffer(sizeof(IndexQueue));
    wavefrontBlock->addSubBuffer(sizeof(IndexQueue));
    wavefrontBlock->addSubBuffer(sizeof(IndexQueue));
    std::vector<BufferBlock::SubBuffer> wavefrontBuffers = wavefrontBlock->allocateBlock();

    pathStateBuffer = wavefrontBuffers[0];
    newPathQueue = wavefrontBuffers[1];
    extensionRayQueue = wavefrontBuffers[2];
    materialRequestQueue = wavefrontBuffers[3];


    for (int i = 0; i < RenderContext::FRAMES_IN_FLIGHT; i++) {
        renderImages[i] = new Image(renderContext, &renderContext->allocator, {renderContext->windowExtent.width, renderContext->windowExtent.height, 1}, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        renderImages[i]->transitionLayout(VK_IMAGE_LAYOUT_GENERAL, submitInfo);
    }

    VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VK_CHECK(vkCreateSampler(renderContext->device, &samplerInfo, nullptr, &defaultSampler));

    VkDescriptorImageInfo renderImageInfo{};
	renderImageInfo.sampler = defaultSampler;
	renderImageInfo.imageView = renderImages[0]->imageView;
	renderImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo pathStateBufferInfo{};
    pathStateBufferInfo.buffer = pathStateBuffer.block->buffer;
    pathStateBufferInfo.offset = pathStateBuffer.offset;
    pathStateBufferInfo.range = pathStateBuffer.size;

    VkDescriptorBufferInfo newPathQueueInfo{};
    newPathQueueInfo.buffer = newPathQueue.block->buffer;
    newPathQueueInfo.offset = newPathQueue.offset;
    newPathQueueInfo.range = newPathQueue.size;

    VkDescriptorBufferInfo extensionQueueInfo{};
    extensionQueueInfo.buffer = extensionRayQueue.block->buffer;
    extensionQueueInfo.offset = extensionRayQueue.offset;
    extensionQueueInfo.range = extensionRayQueue.size;

    VkDescriptorBufferInfo materialRequestQueueInfo{};
    materialRequestQueueInfo.buffer = materialRequestQueue.block->buffer;
    materialRequestQueueInfo.offset = materialRequestQueue.offset;
    materialRequestQueueInfo.range = materialRequestQueue.size;

    VkDescriptorBufferInfo vertexBufferInfo{};
    vertexBufferInfo.buffer = vertexBuffer.block->buffer;
    vertexBufferInfo.offset = vertexBuffer.offset;
    vertexBufferInfo.range = vertexBuffer.size;

    VkDescriptorBufferInfo triangleBufferInfo{};
    triangleBufferInfo.buffer = triangleBuffer.block->buffer;
    triangleBufferInfo.offset = triangleBuffer.offset;
    triangleBufferInfo.range = triangleBuffer.size;

    VkDescriptorBufferInfo bvhBufferInfo{};
    bvhBufferInfo.buffer = bvhBuffer.block->buffer;
    bvhBufferInfo.offset = bvhBuffer.offset;
    bvhBufferInfo.range = bvhBuffer.size;

    VkDescriptorBufferInfo objectBufferInfo{};
    objectBufferInfo.buffer = objectBuffer.block->buffer;
    objectBufferInfo.offset = objectBuffer.offset;
    objectBufferInfo.range = objectBuffer.size;

    VkDescriptorBufferInfo materialBufferInfo{};
    materialBufferInfo.buffer = materialBuffer.block->buffer;
    materialBufferInfo.offset = materialBuffer.offset;
    materialBufferInfo.range = materialBuffer.size;

    VkWriteDescriptorSet graphicsTextureWrite{};
    graphicsTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	graphicsTextureWrite.descriptorCount = 1;
	graphicsTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    graphicsTextureWrite.pImageInfo = &renderImageInfo;

    VkWriteDescriptorSet computeTextureWrite{};
    computeTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	computeTextureWrite.descriptorCount = 1;
	computeTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    computeTextureWrite.pImageInfo = &renderImageInfo;

    VkWriteDescriptorSet pathStateBufferWrite{};
    pathStateBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	pathStateBufferWrite.descriptorCount = 1;
	pathStateBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pathStateBufferWrite.pBufferInfo = &pathStateBufferInfo;

    VkWriteDescriptorSet newPathQueueWrite{};
    newPathQueueWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	newPathQueueWrite.descriptorCount = 1;
	newPathQueueWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    newPathQueueWrite.pBufferInfo = &newPathQueueInfo;

    VkWriteDescriptorSet extensionQueueWrite{};
    extensionQueueWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	extensionQueueWrite.descriptorCount = 1;
	extensionQueueWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    extensionQueueWrite.pBufferInfo = &extensionQueueInfo;

    VkWriteDescriptorSet materialRequestWrite{};
    materialRequestWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	materialRequestWrite.descriptorCount = 1;
	materialRequestWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialRequestWrite.pBufferInfo = &materialRequestQueueInfo;

    VkWriteDescriptorSet vertexBufferWrite{};
    vertexBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	vertexBufferWrite.descriptorCount = 1;
	vertexBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBufferWrite.pBufferInfo = &vertexBufferInfo;

    VkWriteDescriptorSet triangleBufferWrite{};
    triangleBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	triangleBufferWrite.descriptorCount = 1;
	triangleBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    triangleBufferWrite.pBufferInfo = &triangleBufferInfo;

    VkWriteDescriptorSet bvhBufferWrite{};
    bvhBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	bvhBufferWrite.descriptorCount = 1;
	bvhBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bvhBufferWrite.pBufferInfo = &bvhBufferInfo;

    VkWriteDescriptorSet objectBufferWrite{};
    objectBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	objectBufferWrite.descriptorCount = 1;
	objectBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    objectBufferWrite.pBufferInfo = &objectBufferInfo;

    VkWriteDescriptorSet materialBufferWrite{};
    materialBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	materialBufferWrite.descriptorCount = 1;
	materialBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialBufferWrite.pBufferInfo = &materialBufferInfo;
    
    std::vector<VkWriteDescriptorSet> graphicsWrites;
    std::vector<VkWriteDescriptorSet> computeWrites;
    std::vector<VkWriteDescriptorSet> logicWrites;
    std::vector<VkWriteDescriptorSet> newPathWrites;
    std::vector<VkWriteDescriptorSet> materialWrites;
    std::vector<VkWriteDescriptorSet> extensionWrites;
    
    // fragment shader
    addWrite(graphicsWrites, graphicsTextureWrite, 0);

    // logic compute
    addWrite(logicWrites, computeTextureWrite, 0);
    addWrite(logicWrites, pathStateBufferWrite, 1);
    addWrite(logicWrites, newPathQueueWrite, 2);
    addWrite(logicWrites, materialRequestWrite, 3);

    // new path compute
    addWrite(newPathWrites, computeTextureWrite, 0);
    addWrite(newPathWrites, pathStateBufferWrite, 1);
    addWrite(newPathWrites, newPathQueueWrite, 2);
    addWrite(newPathWrites, extensionQueueWrite, 3);

    // material compute
    addWrite(materialWrites, pathStateBufferWrite, 1);
    addWrite(materialWrites, newPathQueueWrite, 2);
    addWrite(materialWrites, materialRequestWrite, 3);
    addWrite(materialWrites, materialBufferWrite, 4);

    // extensions compute
    addWrite(extensionWrites, computeTextureWrite, 0);
    addWrite(extensionWrites, pathStateBufferWrite, 1);
    addWrite(extensionWrites, extensionQueueWrite, 2);
    addWrite(extensionWrites, vertexBufferWrite, 3);
    addWrite(extensionWrites, triangleBufferWrite, 4);
    addWrite(extensionWrites, bvhBufferWrite, 5);
    addWrite(extensionWrites, objectBufferWrite, 6);

    // test compute
    addWrite(computeWrites, computeTextureWrite, 0);
    addWrite(computeWrites, pathStateBufferWrite, 1);
    addWrite(computeWrites, extensionQueueWrite, 3);

    std::string bin = std::filesystem::current_path().generic_string() + "/shaders/bin/";
    graphicsPipeline = new GraphicsPipeline(renderContext, descriptorPool, renderPass, graphicsWrites, &submitInfo, (bin + "raytrace.vert.spv").c_str(), (bin + "raytrace.frag.spv").c_str());
    computePipeline = new ComputePipeline(renderContext, descriptorPool, computeWrites, (bin + "test.comp.spv").c_str(), 0);
    logicPipeline = new ComputePipeline(renderContext, descriptorPool, logicWrites, (bin + "logic.comp.spv").c_str(), 0);
    newPathPipeline = new ComputePipeline(renderContext, descriptorPool, newPathWrites, (bin + "newPath.comp.spv").c_str(), 0);
    materialPipeline = new ComputePipeline(renderContext, descriptorPool, materialWrites, (bin + "material.comp.spv").c_str(), sizeof(MaterialPushConstants));
    extensionPipeline = new ComputePipeline(renderContext, descriptorPool, extensionWrites, (bin + "extension.comp.spv").c_str(), 0);

    // ImGui init
    imguiContext = new ImGuiContext(renderContext->window, renderContext, renderPass, submitInfo);
}

Renderer::~Renderer() {
    delete graphicsPipeline;
    delete computePipeline;
    delete logicPipeline;
    delete newPathPipeline;
    delete materialPipeline;
    delete extensionPipeline;

    vkDestroySampler(renderContext->device, defaultSampler, nullptr);
    vkDestroyCommandPool(renderContext->device, copyCommandPool, nullptr);
    vkDestroyFence(renderContext->device, copyFence, nullptr);

    for (int i = 0; i < RenderContext::FRAMES_IN_FLIGHT; i++) {
        vkDestroyCommandPool(renderContext->device, frames[i].commandPool, nullptr);
        vkDestroyFence(renderContext->device, frames[i].computeReady, nullptr);
        vkDestroyFence(renderContext->device, frames[i].frameReady, nullptr);
        vkDestroySemaphore(renderContext->device, frames[i].swapImageAvailable, nullptr);
        vkDestroySemaphore(renderContext->device, frames[i].renderFinish, nullptr);
        vkDestroySemaphore(renderContext->device, frames[i].computeFinish, nullptr);
        delete framebuffers[i];
        delete renderImages[i];
    }

    delete descriptorPool;
    delete renderPass;
    delete wavefrontBlock;
    delete geometryBlock;
    delete imguiContext;
}

void Renderer::render() {
    // ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(renderContext->window);
    ImGui::NewFrame();

    imguiContext->displayImGui(&renderStats);

    ImGui::Render();

    FrameData frame = frames[frameNumber % RenderContext::FRAMES_IN_FLIGHT];

    vkWaitForFences(renderContext->device, 1, &frame.frameReady, VK_TRUE, 10000000);
	vkResetFences(renderContext->device, 1, &frame.frameReady);

    uint32_t swapchainIndex;
	vkAcquireNextImageKHR(renderContext->device, renderContext->swapchain, 1000000000, frame.swapImageAvailable, nullptr, &swapchainIndex);
    
    // COMPUTE
    VkCommandBufferBeginInfo computeBeginInfo{};
    computeBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    computeBeginInfo.flags = 0;
    computeBeginInfo.pInheritanceInfo = nullptr;


    // TODO:
    // somehow get size of path queue and material
    // somehow get size of extension queue

    // max bounces
    for (int i = 0; i < 5; i++) {    
        // logic
        VK_CHECK(vkBeginCommandBuffer(frame.logicCmdBuffer, &computeBeginInfo));

        vkCmdResetQueryPool(frame.logicCmdBuffer, renderContext->queryPool, 1, RenderContext::QUERY_SIZE - 1);

        // only record first so the total is accurate
        if (i == 0) {
            vkCmdResetQueryPool(frame.logicCmdBuffer, renderContext->queryPool, 0, RenderContext::QUERY_SIZE);
            vkCmdWriteTimestamp(frame.logicCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderContext->queryPool, 0);
        }
        vkCmdWriteTimestamp(frame.logicCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderContext->queryPool, 1);

        vkCmdBindPipeline(frame.logicCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, logicPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.logicCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, logicPipeline->pipelineLayout, 0, 1, &logicPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.logicCmdBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);
        
        vkCmdWriteTimestamp(frame.logicCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, renderContext->queryPool, 2);

        vkEndCommandBuffer(frame.logicCmdBuffer);

        VkPipelineStageFlags waitStage[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
        VkSubmitInfo logicSubmitInfo{};
        logicSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        logicSubmitInfo.commandBufferCount = 1;
        logicSubmitInfo.pCommandBuffers = &frame.logicCmdBuffer;
        logicSubmitInfo.waitSemaphoreCount = i == 0 ? 0 : 1;
        logicSubmitInfo.pWaitSemaphores = i == 0 ? nullptr : &frame.computeFinish;
        logicSubmitInfo.signalSemaphoreCount = 1;
        logicSubmitInfo.pSignalSemaphores = &frame.computeFinish;
        logicSubmitInfo.pWaitDstStageMask = waitStage;
        
        VK_CHECK(vkQueueSubmit(renderContext->graphicsQueue, 1, &logicSubmitInfo, frame.frameReady));
        VK_CHECK(vkWaitForFences(renderContext->device, 1, &frame.frameReady, VK_TRUE, 999999999));
        VK_CHECK(vkResetFences(renderContext->device, 1, &frame.frameReady));

        // new path and material
        VK_CHECK(vkBeginCommandBuffer(frame.materialNewPathCmdBuffer, &computeBeginInfo));
        
        vkCmdWriteTimestamp(frame.materialNewPathCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderContext->queryPool, 3);

        vkCmdBindPipeline(frame.materialNewPathCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, newPathPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.materialNewPathCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, newPathPipeline->pipelineLayout, 0, 1, &newPathPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.materialNewPathCmdBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);

        MaterialPushConstants constants = {dispatchCount};

        vkCmdPushConstants(frame.materialNewPathCmdBuffer, materialPipeline->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(MaterialPushConstants), &constants);
        vkCmdBindPipeline(frame.materialNewPathCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, materialPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.materialNewPathCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, materialPipeline->pipelineLayout, 0, 1, &materialPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.materialNewPathCmdBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);

        vkCmdWriteTimestamp(frame.materialNewPathCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, renderContext->queryPool, 4);
        vkEndCommandBuffer(frame.materialNewPathCmdBuffer);

        VkSubmitInfo materialNewPathSubmitInfo{};
        materialNewPathSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        materialNewPathSubmitInfo.commandBufferCount = 1;
        materialNewPathSubmitInfo.pCommandBuffers = &frame.materialNewPathCmdBuffer;
        materialNewPathSubmitInfo.waitSemaphoreCount = 1;
        materialNewPathSubmitInfo.pWaitSemaphores = &frame.computeFinish;
        materialNewPathSubmitInfo.signalSemaphoreCount = 1;
        materialNewPathSubmitInfo.pSignalSemaphores = &frame.computeFinish;
        materialNewPathSubmitInfo.pWaitDstStageMask = waitStage;

        VK_CHECK(vkQueueSubmit(renderContext->graphicsQueue, 1, &materialNewPathSubmitInfo, frame.frameReady));
        VK_CHECK(vkWaitForFences(renderContext->device, 1, &frame.frameReady, VK_TRUE, 999999999));
        VK_CHECK(vkResetFences(renderContext->device, 1, &frame.frameReady));

        // extension
        VK_CHECK(vkBeginCommandBuffer(frame.extensionCmdBuffer, &computeBeginInfo));

        vkCmdWriteTimestamp(frame.extensionCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderContext->queryPool, 5);
       
        vkCmdBindPipeline(frame.extensionCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, extensionPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.extensionCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, extensionPipeline->pipelineLayout, 0, 1, &extensionPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.extensionCmdBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);

        vkCmdWriteTimestamp(frame.extensionCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, renderContext->queryPool, 6);
        vkEndCommandBuffer(frame.extensionCmdBuffer);

        VkSubmitInfo extensionSubmitInfo{};
        extensionSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        extensionSubmitInfo.commandBufferCount = 1;
        extensionSubmitInfo.pCommandBuffers = &frame.extensionCmdBuffer;
        extensionSubmitInfo.waitSemaphoreCount = 1;
        extensionSubmitInfo.pWaitSemaphores = &frame.computeFinish;
        extensionSubmitInfo.signalSemaphoreCount = 1;
        extensionSubmitInfo.pSignalSemaphores = &frame.computeFinish;
        extensionSubmitInfo.pWaitDstStageMask = waitStage;
        
        VK_CHECK(vkQueueSubmit(renderContext->graphicsQueue, 1, &extensionSubmitInfo, frame.frameReady));
        VK_CHECK(vkWaitForFences(renderContext->device, 1, &frame.frameReady, VK_TRUE, 999999999));
        VK_CHECK(vkResetFences(renderContext->device, 1, &frame.frameReady));

        dispatchCount++;
    }

    // GRAPHICS
    VkCommandBufferBeginInfo graphicsBeginInfo{};
    graphicsBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    graphicsBeginInfo.flags = 0;
    graphicsBeginInfo.pInheritanceInfo = nullptr;

    VkClearColorValue clearColor = {0.f, 1.f, 0.f};
	VkClearValue clearValue;
	clearValue.color = clearColor;

	VkRenderPassBeginInfo rpBeginInfo = {};
	rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBeginInfo.clearValueCount = 1;
	rpBeginInfo.pClearValues = &clearValue;
    rpBeginInfo.framebuffer = framebuffers[frameNumber % RenderContext::FRAMES_IN_FLIGHT]->framebuffer;
	rpBeginInfo.renderPass = renderPass->renderPass;
	rpBeginInfo.renderArea.offset = {0, 0};
	rpBeginInfo.renderArea.extent = renderContext->windowExtent;

    VK_CHECK(vkBeginCommandBuffer(frame.graphicsCmdBuffer, &graphicsBeginInfo));

    vkCmdWriteTimestamp(frame.graphicsCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderContext->queryPool, 7);
    vkCmdBeginRenderPass(frame.graphicsCmdBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(frame.graphicsCmdBuffer, 0, 1, &graphicsPipeline->vertexBuffer->buffer, &offset);
	vkCmdBindIndexBuffer(frame.graphicsCmdBuffer, graphicsPipeline->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(frame.graphicsCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->pipelineLayout, 0, 1, &graphicsPipeline->descriptorSet, 0, nullptr);
    vkCmdBindPipeline(frame.graphicsCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->pipeline);
    vkCmdDrawIndexed(frame.graphicsCmdBuffer, 6, 1, 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.graphicsCmdBuffer);
    vkCmdEndRenderPass(frame.graphicsCmdBuffer);

    vkCmdWriteTimestamp(frame.graphicsCmdBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, renderContext->queryPool, 8);
	vkEndCommandBuffer(frame.graphicsCmdBuffer);

    // submit and present to queue
	VkPipelineStageFlags waitStageMasks[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore graphicsSemaphores[] = {frame.swapImageAvailable, frame.computeFinish};
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.graphicsCmdBuffer;
    submitInfo.waitSemaphoreCount = 2;
    submitInfo.pWaitSemaphores = graphicsSemaphores;
    submitInfo.pWaitDstStageMask = waitStageMasks;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frame.renderFinish;
    VK_CHECK(vkQueueSubmit(renderContext->graphicsQueue, 1, &submitInfo, frame.frameReady));

    VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &renderContext->swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinish;
	presentInfo.pImageIndices = &swapchainIndex;

	VK_CHECK(vkQueuePresentKHR(renderContext->graphicsQueue, &presentInfo));
	vkQueueWaitIdle(renderContext->graphicsQueue);
    
    // readback: is there a way to only get the size and not the indices which would slow down readback ?
    // use subregions to copy into a seperate buffer ?
    // also combine all buffers into one memory allocation and use offsets for more optimal reads ?
    // can do in one copy command using multiple VkBufferCopy structs, but needs to be in one buffer !

    // timing
    uint64_t times[RenderContext::QUERY_SIZE * 2];
    vkGetQueryPoolResults(
        renderContext->device, 
        renderContext->queryPool, 
        0, 
        RenderContext::QUERY_SIZE, 
        RenderContext::QUERY_SIZE * 2 * sizeof(uint64_t), 
        times,
        2 * sizeof(uint64_t), 
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
    );

    renderStats.logicTime = float(times[4] - times[2]) / 1000000.0f;
    renderStats.materialNewPathTime = float(times[8] - times[6]) / 1000000.0f;
    renderStats.extensionTime = float(times[12] - times[10]) / 1000000.0f;
    renderStats.graphicsTime = float(times[16] - times[14]) / 1000000.0f;
    renderStats.totalTime = float(times[16] - times[0]) / 1000000.0f;

    /// rACHIT WAs HERE
    frameNumber++;
}

void Renderer::addWrite(std::vector<VkWriteDescriptorSet>& sets, VkWriteDescriptorSet newSet, uint binding) {
    newSet.dstBinding = binding;
    sets.push_back(newSet);
}