#version 410 core
out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_ScreenTexture;
uniform float u_Threshold;
uniform float u_Exposure;

void main() {
    vec3 color = texture(u_ScreenTexture, v_TexCoord).rgb;
    // 【核心】：在判断阈值前，先乘以曝光系数！
    // 这样 UI 里的阈值(通常是1.0)才符合人类直觉，否则你得输入几万的物理数值去对比
    float brightness = dot(color * u_Exposure, vec3(0.2126, 0.7152, 0.0722));
    
    if(brightness > u_Threshold) {
        FragColor = vec4(color, 1.0); // 提取原始的超高物理能量！
    } else {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0); // 其余部分填黑
    }
}