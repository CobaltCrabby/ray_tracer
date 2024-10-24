#version 460

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 color;

layout (location = 0) out vec3 outColor;

layout(push_constant) uniform constants {
	vec4 data;
	mat4 render_matrix;
} PushConstants;

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 proj;
    mat4 view;
    mat4 viewProj;
} cameraData;

struct ObjectData {
    mat4 model;
};

layout (std140, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

void main() {
    mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;
    mat4 transformMatrix = cameraData.viewProj * modelMatrix;
    gl_Position = transformMatrix * vec4(position, 1.0f);
    outColor = color;
}