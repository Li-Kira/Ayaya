#version 450 core

layout(location = 0) out vec4 FragColor;

// 【修复 1】：天空盒采样向量必须是 vec3
layout(location = 0) in vec3 v_TexCoords;

layout(binding = 0) uniform samplerCube u_Skybox;

// 【修复 2】：将散装的 uniform 放入 Push Constant (需与 vert 保持一致的结构)
layout(push_constant) uniform Constants {
    mat4 u_Projection;
    mat4 u_View;
    mat4 u_Transform;
    float u_Intensity;
} pc;

void main() {    
    vec3 envColor = texture(u_Skybox, v_TexCoords).rgb;
    
    FragColor = vec4(envColor * pc.u_Intensity, 1.0);
}