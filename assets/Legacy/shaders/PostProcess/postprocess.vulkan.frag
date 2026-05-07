#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

// 保持 3 个 binding 存在，满足 DescriptorSet Layout 的坑位要求
layout(binding = 0) uniform sampler2D u_ScreenTexture; 
layout(binding = 1) uniform sampler2D u_SelectionTexture;
layout(binding = 2) uniform sampler2D u_BloomTexture;

void main() {
    // 采样来自 VulkanClearPass 的画面
    vec4 color = texture(u_ScreenTexture, v_TexCoord);
    // FragColor = vec4(color.rgb, 1.0);
    FragColor = vec4(1.0, 0.0, 1.0, 1.0);
}