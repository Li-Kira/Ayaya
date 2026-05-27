#version 450 core

layout (location = 0) in vec3 a_Position;
layout(location = 0) out vec3 v_TexCoords;

// 【修复】：精简至 68 字节，完美适配所有 Vulkan 设备
layout(push_constant) uniform Constants {
    mat4 u_ViewProjection;
    float u_Intensity;
} pc;

void main() {
    v_TexCoords = a_Position;
    v_TexCoords.y = -v_TexCoords.y; // 补偿 IBL 源 HDR 的 stbi flip

    // 直接使用 CPU 算好的 VP 矩阵
    vec4 pos = pc.u_ViewProjection * vec4(a_Position, 1.0);

    // 深度强制置为 1.0 (Vulkan 远裁剪面也是 1.0)
    gl_Position = pos.xyww;
}
