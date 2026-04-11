#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

// 【测试修复】：临时注释掉这三行，告诉 Vulkan 我们现在不需要任何贴图！
layout(binding = 0) uniform sampler2D u_ScreenTexture; 
layout(binding = 1) uniform sampler2D u_SelectionTexture;
layout(binding = 2) uniform sampler2D u_BloomTexture;

layout(push_constant) uniform PushConstants {
    float u_Exposure;   
    vec2  u_TexelSize; 
    int   u_ToneMappingType; 
    int   u_EnableBloom;
    float u_BloomIntensity;
};

void main() {
    // 暴力输出洋红色！
    // FragColor = vec4(1.0, 0.0, 1.0, 1.0);
    vec4 color = texture(u_ScreenTexture, v_TexCoord);
    
    // 为了证明它是经过我们的 Shader 处理的，我们将背景色做个反相 (Color Inversion)
    FragColor = vec4(1.0 - color.rgb, 1.0);
}