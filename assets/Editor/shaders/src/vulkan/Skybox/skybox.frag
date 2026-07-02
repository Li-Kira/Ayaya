#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 v_TexCoords;

layout(set = 1, binding = 0) uniform samplerCube u_Skybox;

layout(push_constant) uniform Constants {
    mat4 u_ViewProjection;
    float u_Intensity;
} pc;

void main() {
    vec3 envColor = texture(u_Skybox, v_TexCoords).rgb;
    FragColor = vec4(envColor * pc.u_Intensity, 1.0);
}
