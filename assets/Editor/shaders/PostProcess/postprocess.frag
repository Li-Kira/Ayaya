#version 410 core
out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_ScreenTexture; 
uniform float u_Exposure = 1.0;    

void main() {
    // 读取带有 Alpha 的完整数据
    vec4 hdrData = texture(u_ScreenTexture, v_TexCoord);
    
    // 仅对 RGB 通道进行色调映射和 Gamma 校正
    vec3 mapped = vec3(1.0) - exp(-hdrData.rgb * u_Exposure);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    
    // 核心：原样输出 Alpha 通道！(1.0 为物理实体，0.0 为虚空背景)
    FragColor = vec4(mapped, hdrData.a);
}