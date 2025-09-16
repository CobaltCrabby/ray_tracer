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

        VkCommandBufferAllocateInfo bufferAllocateInfo{};
        bufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferAllocateInfo.commandBufferCount = 1;
        bufferAllocateInfo.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(context->device, &bufferAllocateInfo, &frames[i].commandBuffer));

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(renderContext->device, &fenceInfo, nullptr, &frames[i].frameReady));

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(renderContext->device, &semaphoreInfo, nullptr, &frames[i].renderFinish));
        VK_CHECK(vkCreateSemaphore(renderContext->device, &semaphoreInfo, nullptr, &frames[i].swapImageAvailable));
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

    // make read_obj part of TLAS so you can directly write to tri buffer
    TLAS tlas;
    tlas.readObj("assets/rb_low.obj");

    // for (int i = 0; i < tlas.vertices.size(); i++) {
        // std::cout << i << " " << glm::to_string(tlas.vertices[i].position) << std::endl;
    // }

    descriptorPool = new DescriptorPool(renderContext);
    vertexBuffer = new Buffer(renderContext->device, copyCommandPool, copyCommandBuffer, &copyFence, renderContext->graphicsQueue, renderContext->allocator, sizeof(TLAS::Vertex) * tlas.vertices.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) tlas.vertices.data());
    triangleBuffer = new Buffer(renderContext->device, copyCommandPool, copyCommandBuffer, &copyFence, renderContext->graphicsQueue, renderContext->allocator, sizeof(TLAS::Triangle) * tlas.triangles.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) tlas.triangles.data());
    BVHBuffer = new Buffer(renderContext->device, copyCommandPool, copyCommandBuffer, &copyFence, renderContext->graphicsQueue, renderContext->allocator, sizeof(TLAS::BVHNode) * tlas.bvhNodes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, (void*) tlas.bvhNodes.data());
    pathStateBuffer = new Buffer(renderContext->allocator, sizeof(PathState), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    newPathQueue = new Buffer(renderContext->allocator, sizeof(IndexQueue), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    extensionRayQueue = new Buffer(renderContext->allocator, sizeof(IndexQueue), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    materialRequestQueue = new Buffer(renderContext->allocator, sizeof(IndexQueue), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    
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
    pathStateBufferInfo.buffer = pathStateBuffer->buffer;
    pathStateBufferInfo.offset = 0;
    pathStateBufferInfo.range = sizeof(PathState);

    VkDescriptorBufferInfo newPathQueueInfo{};
    newPathQueueInfo.buffer = newPathQueue->buffer;
    newPathQueueInfo.offset = 0;
    newPathQueueInfo.range = sizeof(IndexQueue);

    VkDescriptorBufferInfo extensionQueueInfo{};
    extensionQueueInfo.buffer = extensionRayQueue->buffer;
    extensionQueueInfo.offset = 0;
    extensionQueueInfo.range = sizeof(IndexQueue);

    VkDescriptorBufferInfo materialRequestQueueInfo{};
    materialRequestQueueInfo.buffer = materialRequestQueue->buffer;
    materialRequestQueueInfo.offset = 0;
    materialRequestQueueInfo.range = sizeof(IndexQueue);

    VkDescriptorBufferInfo vertexBufferInfo{};
    vertexBufferInfo.buffer = vertexBuffer->buffer;
    vertexBufferInfo.offset = 0;
    vertexBufferInfo.range = sizeof(TLAS::Vertex) * tlas.vertices.size();

    VkDescriptorBufferInfo triangleBufferInfo{};
    triangleBufferInfo.buffer = triangleBuffer->buffer;
    triangleBufferInfo.offset = 0;
    triangleBufferInfo.range = sizeof(TLAS::Triangle) * tlas.triangles.size();

    VkDescriptorBufferInfo BVHBufferInfo{};
    BVHBufferInfo.buffer = BVHBuffer->buffer;
    BVHBufferInfo.offset = 0;
    BVHBufferInfo.range = sizeof(TLAS::BVHNode) * tlas.bvhNodes.size();

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

    VkWriteDescriptorSet BVHBufferWrite{};
    BVHBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	BVHBufferWrite.descriptorCount = 1;
	BVHBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    BVHBufferWrite.pBufferInfo = &BVHBufferInfo;
    
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
    //addWrite(logicWrites, materialRequestWrite, 3);

    // new path compute
    addWrite(newPathWrites, computeTextureWrite, 0);
    addWrite(newPathWrites, pathStateBufferWrite, 1);
    addWrite(newPathWrites, newPathQueueWrite, 2);
    addWrite(newPathWrites, extensionQueueWrite, 3);

    // material compute
    addWrite(materialWrites, pathStateBufferWrite, 1);
    addWrite(materialWrites, newPathQueueWrite, 2);
    addWrite(materialWrites, materialRequestWrite, 3);

    // extensions compute
    addWrite(extensionWrites, computeTextureWrite, 0);
    addWrite(extensionWrites, pathStateBufferWrite, 1);
    addWrite(extensionWrites, extensionQueueWrite, 2);
    addWrite(extensionWrites, vertexBufferWrite, 3);
    addWrite(extensionWrites, triangleBufferWrite, 4);
    addWrite(extensionWrites, BVHBufferWrite, 5);

    // test compute
    addWrite(computeWrites, computeTextureWrite, 0);
    addWrite(computeWrites, pathStateBufferWrite, 1);
    addWrite(computeWrites, extensionQueueWrite, 3);

    std::string bin = std::filesystem::current_path().generic_string() + "/shaders/bin/";
    graphicsPipeline = new GraphicsPipeline(renderContext, descriptorPool, renderPass, graphicsWrites, &submitInfo, (bin + "raytrace.vert.spv").c_str(), (bin + "raytrace.frag.spv").c_str());
    computePipeline = new ComputePipeline(renderContext, descriptorPool, computeWrites, (bin + "test.comp.spv").c_str());
    logicPipeline = new ComputePipeline(renderContext, descriptorPool, logicWrites, (bin + "logic.comp.spv").c_str());
    newPathPipeline = new ComputePipeline(renderContext, descriptorPool, newPathWrites, (bin + "newPath.comp.spv").c_str());
    materialPipeline = new ComputePipeline(renderContext, descriptorPool, materialWrites, (bin + "material.comp.spv").c_str());
    extensionPipeline = new ComputePipeline(renderContext, descriptorPool, extensionWrites, (bin + "extension.comp.spv").c_str());

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
        vkDestroyFence(renderContext->device, frames[i].frameReady, nullptr);
        vkDestroySemaphore(renderContext->device, frames[i].swapImageAvailable, nullptr);
        vkDestroySemaphore(renderContext->device, frames[i].renderFinish, nullptr);
        delete framebuffers[i];
        delete renderImages[i];
    }

    delete descriptorPool;
    delete renderPass;
    delete vertexBuffer;
    delete triangleBuffer;
    delete BVHBuffer;
    delete pathStateBuffer;
    delete newPathQueue;
    delete extensionRayQueue;
    delete materialRequestQueue;
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

    if (frameNumber == 0) {
        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(extensionRayQueue->allocator, extensionRayQueue->allocation, &allocInfo);
        IndexQueue* readback = (IndexQueue*) allocInfo.pMappedData;
        std::cout << "before - size: " << readback->size << " pool size: " << renderContext->windowExtent.width * renderContext->windowExtent.height << std::endl;
    }

    vkWaitForFences(renderContext->device, 1, &frame.frameReady, VK_TRUE, 10000000);
	vkResetFences(renderContext->device, 1, &frame.frameReady);

    uint32_t swapchainIndex;
	vkAcquireNextImageKHR(renderContext->device, renderContext->swapchain, 1000000000, frame.swapImageAvailable, nullptr, &swapchainIndex);
    
    // graphics
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

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

    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    // reset timestamps
    vkCmdResetQueryPool(frame.commandBuffer, renderContext->queryPool, 0, RenderContext::QUERY_SIZE);
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderContext->queryPool, 0);

    // TODO:
    // somehow get size of path queue and material
    // somehow get size of extension queue
    // i need to seperate the kernels into different command buffers to i can get the size after dispatch
    // this will also make timing the individual kernels possible which is nice ig but slower overall

    for (int i = 0; i < 1; i++) {
        // logic run
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, logicPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, logicPipeline->pipelineLayout, 0, 1, &logicPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.commandBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);
        
        // pipeline barrier the buffers (NEW PATH AND MATERIAL REQUEST QUEUES)
        VkBufferMemoryBarrier newPathMemoryBarrier{};
        newPathMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        newPathMemoryBarrier.buffer = newPathQueue->buffer;
        newPathMemoryBarrier.size = sizeof(IndexQueue);
        newPathMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        newPathMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        newPathMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        newPathMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        newPathMemoryBarrier.offset = 0;
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &newPathMemoryBarrier, 0, nullptr);

        VkBufferMemoryBarrier materialBufferMemoryBarrier{};
        materialBufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        materialBufferMemoryBarrier.buffer = materialRequestQueue->buffer;
        materialBufferMemoryBarrier.size = sizeof(IndexQueue);
        materialBufferMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        materialBufferMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        materialBufferMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        materialBufferMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        materialBufferMemoryBarrier.offset = 0;
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &materialBufferMemoryBarrier, 0, nullptr);

        // new path and material run
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, newPathPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, newPathPipeline->pipelineLayout, 0, 1, &newPathPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.commandBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);

        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, materialPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, materialPipeline->pipelineLayout, 0, 1, &materialPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.commandBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);

        // pipeline barrier the buffer (EXTENSION REQUEST)
        VkBufferMemoryBarrier extensionMemoryBarrier{};
        extensionMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        extensionMemoryBarrier.buffer = extensionRayQueue->buffer;
        extensionMemoryBarrier.size = sizeof(IndexQueue);
        extensionMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        extensionMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        extensionMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        extensionMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        extensionMemoryBarrier.offset = 0;
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &extensionMemoryBarrier, 0, nullptr);

        // extension pipeline run
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, extensionPipeline->pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, extensionPipeline->pipelineLayout, 0, 1, &extensionPipeline->descriptorSet, 0, nullptr);
        vkCmdDispatch(frame.commandBuffer, ceil(renderContext->windowExtent.width * renderContext->windowExtent.height / 64.f), 1, 1);

        // pipeline barrier the buffer (PATH STATE WRITES FROM EXTENSION)
        VkBufferMemoryBarrier pathStateMemoryBarrier{};
        pathStateMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        pathStateMemoryBarrier.buffer = pathStateBuffer->buffer;
        pathStateMemoryBarrier.size = sizeof(PathState);
        pathStateMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        pathStateMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        pathStateMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pathStateMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pathStateMemoryBarrier.offset = 0;
        vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &pathStateMemoryBarrier, 0, nullptr);
        
        vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, renderContext->queryPool, 1);
    }

    // pipeline barrier the image
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.image = renderImages[0]->image;
    imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0 , nullptr, 1, &imageMemoryBarrier);

    // graphics pipeline run
    vkCmdBeginRenderPass(frame.commandBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &graphicsPipeline->vertexBuffer->buffer, &offset);
	vkCmdBindIndexBuffer(frame.commandBuffer, graphicsPipeline->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->pipelineLayout, 0, 1, &graphicsPipeline->descriptorSet, 0, nullptr);
    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->pipeline);
    vkCmdDrawIndexed(frame.commandBuffer, 6, 1, 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.commandBuffer);
    vkCmdEndRenderPass(frame.commandBuffer);

    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, renderContext->queryPool, 2);
	vkEndCommandBuffer(frame.commandBuffer);

    // submit and present to queue
	VkPipelineStageFlags waitStageMasks[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.swapImageAvailable;
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
    
    if (frameNumber == 0) {
        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(extensionRayQueue->allocator, extensionRayQueue->allocation, &allocInfo);
        IndexQueue* readback = (IndexQueue*) allocInfo.pMappedData;
        std::cout << "after - size: " << readback->size << " pool size: " << renderContext->windowExtent.width * renderContext->windowExtent.height << std::endl;
    }

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

    renderStats.computeTime = float(times[2] - times[0]) / 1000000.0f;
    renderStats.totalTime = float(times[4] - times[0]) / 1000000.0f;
    /// rACHIT WAs HERE
    frameNumber++;
}

void Renderer::addWrite(std::vector<VkWriteDescriptorSet>& sets, VkWriteDescriptorSet newSet, uint binding) {
    newSet.dstBinding = binding;
    sets.push_back(newSet);
}
