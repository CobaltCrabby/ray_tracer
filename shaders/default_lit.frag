#version 450

layout (location = 0) in vec3 inColor;
layout (location = 0) out vec4 fragColor;

layout (set = 0, binding = 1) uniform SceneData {
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

void main() {
    fragColor = vec4(inColor + sceneData.ambientColor.xyz, 1.f);
}