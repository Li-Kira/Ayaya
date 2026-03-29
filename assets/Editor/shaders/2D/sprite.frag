#version 410 core

in vec2 v_TexCoord;
out vec4 FragColor;

uniform vec4 u_Color;
uniform sampler2D u_Texture;
uniform bool u_UseTexture;

// 接收物理曝光衰减系数
uniform float u_ExposureInverse;

void main() {
    vec4 texColor = u_UseTexture ? texture(u_Texture, v_TexCoord) : vec4(1.0);
    vec4 finalColor = texColor * u_Color;
    
    // 如果像素极其透明，直接抛弃，节约显卡性能并防止深度遮挡
    if (finalColor.a < 0.01) {
        discard;
    }

    // 核心修复：将颜色乘以逆曝光系数，使 2D 精灵在 HDR 背景下能保持原本的亮丽色彩！
    FragColor = vec4(finalColor.rgb * u_ExposureInverse, finalColor.a);
}