#version 450

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

void main() {
    mat4 transformMatrix = cameraData.viewProj * PushConstants.render_matrix;
    gl_Position = transformMatrix * vec4(position, 1.0f);
    outColor = color;
}