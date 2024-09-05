#version 450

layout(location = 0) out vec3 outColor;

void main() {
    const vec3 positions[6] = vec3[6](
        vec3(0.9f, 0.9f, 0.f),
        vec3(-0.9f, 0.9f, 0.f),
        vec3(0.9f, -0.9f, 0.f),
        vec3(-0.9f, 0.9f, 0.f),
        vec3(0.9f, -0.9f, 0.f),
        vec3(-0.9f, -0.9f, 0.f)
    );

    const vec3 colors[6] = vec3[6](
        vec3(0.f, 1.f, 0.f), //green blue red
        vec3(0.f, 0.f, 1.f),
        vec3(1.f, 0.f, 0.f),
        vec3(0.f, 0.f, 1.f),
        vec3(1.f, 0.f, 0.f),
        vec3(0.f, 0.f, 0.f)
    );

	gl_Position = vec4(positions[gl_VertexIndex], 1.f);
    outColor = colors[gl_VertexIndex];
}