#include "vulkan/vulkan_core.h"
#include <descriptor_pool.hpp>

DescriptorPool::DescriptorPool(RenderContext* context, VkDescriptorPoolCreateFlags flags, std::vector<VkDescriptorPoolSize> sizes) {
    renderContext = context;
	if (sizes.empty()) {
			sizes = {
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32},
			{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
			{VK_DESCRIPTOR_TYPE_SAMPLER, 2},
		};
	}

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = flags;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = (uint32_t)sizes.size();
	poolInfo.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(context->device, &poolInfo, nullptr, &pool);
}

DescriptorPool::~DescriptorPool() {
    vkDestroyDescriptorPool(renderContext->device, pool, nullptr);
}