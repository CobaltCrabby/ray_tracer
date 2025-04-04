#pragma once
#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include "descriptor_pool.hpp"
#include "pipeline.hpp"
#include "render_context.hpp"

class ImGuiContext {
    public:
        DescriptorPool* descriptorPool;
        RenderContext* renderContext;

        ImGuiContext(SDL_Window* window, RenderContext* context, RenderPass* renderpass, SubmitInfo immediateSubmitInfo);
        ~ImGuiContext();
};