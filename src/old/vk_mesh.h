#pragma once

#include <vk_types.h>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct VertexInputDescription {
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::vector<VkVertexInputBindingDescription> bindings;
    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {
    glm::vec3 position;
    glm::vec2 uv;

    static VertexInputDescription get_vertex_description();
};

struct Mesh {
    std::vector<Vertex> vertices;
    AllocatedBuffer vertexBuffer;

    bool load_from_obj(char* filePath);
};