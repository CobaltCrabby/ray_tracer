#include <exception>
#include <imgui.hpp>
#include <SDL_vulkan.h>
#include <SDL.h>
#include <renderpass.hpp>

void ImGuiContext::displayImGui(ImGuiStats* stats) {
	ImGui::Begin("PathTracer");
	ImVec2 windowSize = {600, 600};
	ImGui::SetWindowSize(windowSize);

	if (ImGui::CollapsingHeader("Timings")) {
		ImGui::Indent(16.f);
		ImGui::Text("compute dispatches: %.3fms", stats->computeTime);
		ImGui::Text("total: %.1ffps", 1.f / (stats->totalTime / 1000.f));
		ImGui::Unindent(16.f);
	}

	ImGui::End();
}

ImGuiContext::ImGuiContext(SDL_Window* window, RenderContext* context, RenderPass* renderpass, SubmitInfo immediateSubmitInfo) {
    renderContext = context;

    std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

    descriptorPool = new DescriptorPool(context, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, poolSizes);

    //initialize library
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(window);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = context->instance;
	init_info.PhysicalDevice = context->physicalDevice;
	init_info.Device = context->device;
	init_info.Queue = context->graphicsQueue;
	init_info.DescriptorPool = descriptorPool->pool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.RenderPass = renderpass->renderPass;
	init_info.QueueFamily = context->graphicsQueueFamily;
	init_info.Subpass = 0;

	ImGui_ImplVulkan_Init(&init_info);	

	//execute a gpu command to upload imgui font textures
    VkCommandBuffer cmd = immediateSubmitInfo.submitBuffer;
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
	ImGui_ImplVulkan_CreateFontsTexture();
	VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;

	VK_CHECK(vkQueueSubmit(immediateSubmitInfo.queue, 1, &submitInfo, *immediateSubmitInfo.submitFence));
	vkWaitForFences(context->device, 1, immediateSubmitInfo.submitFence, VK_TRUE, 9999999999);
	vkResetFences(context->device, 1, immediateSubmitInfo.submitFence);

	//clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontsTexture();
}

ImGuiContext::~ImGuiContext() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    vkDestroyDescriptorPool(renderContext->device, descriptorPool->pool, nullptr);
}