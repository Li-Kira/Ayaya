#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 v_TexCoords;

layout(set = 1, binding = 0) uniform samplerCube u_Skybox;

// 【修复】：必须和 vert 里的结构完全一模一样！
layout(push_constant) uniform Constants {
    mat4 u_ViewProjection;
    float u_Intensity;
} pc;

void main() {    
    // 采样 Cubemap
    vec3 envColor = texture(u_Skybox, v_TexCoords).rgb;
    
    FragColor = vec4(envColor, 1.0);
    // FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}