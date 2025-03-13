#include "vulkan/vulkan_core.h"
#include <framebuffer.h>

Framebuffer::Framebuffer(RenderContext* renderContext, std::vector<VkImageView> attachment, RenderPass* rp) {
    this->renderContext = renderContext;

    VkFramebufferCreateInfo framebufferCreateInfo{};
    framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferCreateInfo.renderPass = rp->renderPass;
    framebufferCreateInfo.pAttachments = attachment.data();
    framebufferCreateInfo.attachmentCount = attachment.size();
    framebufferCreateInfo.height = renderContext->windowExtent.height;
    framebufferCreateInfo.width = renderContext->windowExtent.width;
    framebufferCreateInfo.layers = 1;

    VK_CHECK(vkCreateFramebuffer(renderContext->device, &framebufferCreateInfo, nullptr, &framebuffer));
}

Framebuffer::~Framebuffer() {
    vkDestroyFramebuffer(renderContext->device, framebuffer, nullptr);
}