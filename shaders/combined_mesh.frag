#version 450

layout (location = 0) in vec3 inColor;
layout (location = 0) out vec4 fragColor;

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

void main() {
    fragColor = vec4(inColor + camSceneData.scene.ambientColor.xyz, 1.f);
}