#version 460

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout(push_constant) uniform constants {
	vec4 data;
	mat4 render_matrix;
} PushConstants;

struct CameraData {
    mat4 proj;
    mat4 view;
    mat4 viewProj;
};

struct SceneData {
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
};

layout (set = 0, binding = 0) uniform CamSceneBuffer {
    CameraData camera;
    SceneData scene;
} camSceneData;

struct ObjectData {
    mat4 model;
};

struct colorData {
    vec3 color;
};

layout (std140, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

layout (std140, set = 1, binding = 1) readonly buffer ColorBuffer {
    colorData colors[];
} colorBuffer;

void main() {
    mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;
    mat4 transformMatrix = camSceneData.camera.viewProj * modelMatrix;
    gl_Position = transformMatrix * vec4(position, 1.f);
    outColor = dot(vColor, normalize(vec3(0.2f, 1.f, 0.2f))) + colorBuffer.colors[gl_BaseInstance].color;
}