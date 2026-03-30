#version 410 core
out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_ScreenTexture; 
uniform sampler2D u_SelectionTexture;
uniform float u_Exposure = 1.0;   
uniform vec2 u_TexelSize; 

// 【新增】：色调映射算法选择器 (0 = Reinhard/Exposure, 1 = ACES)
uniform int u_ToneMappingType = 1; 

// ACES 电影级色调映射曲线
vec3 ACESFilm(vec3 x) {
    float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    // 读取带有 Alpha 的完整数据 (G-Buffer 传过来的 HDR 颜色)
    vec4 hdrData = texture(u_ScreenTexture, v_TexCoord);
    
    // 1. 物理曝光缩放
    vec3 hdrColor = hdrData.rgb * u_Exposure;
    
    vec3 mapped = vec3(0.0);
    
    // 2. 色调映射 (Tone Mapping)：将无限的 HDR 能量压回 0.0 ~ 1.0 的 LDR 范围
    if (u_ToneMappingType == 0) {
        // 传统的基于自然底数 e 的曝光映射
        mapped = vec3(1.0) - exp(-hdrColor);
    } else {
        // ACES 电影级曲线：高光衰减更平滑，对比度更具张力
        mapped = ACESFilm(hdrColor);
    }
    
    // 3. 伽马校正 (Gamma Correction)：线性空间转 sRGB 显示空间
    mapped = pow(mapped, vec3(1.0 / 2.2));
    
    vec3 finalColor = mapped;
    
    // ==========================================
    // 屏幕空间完美描边魔法 (保持不变)
    // ==========================================
    float maskCenter = texture(u_SelectionTexture, v_TexCoord).r;

    vec2 offset = u_TexelSize * 2.0;
    float maskN  = texture(u_SelectionTexture, v_TexCoord + vec2( 0.0,      offset.y)).r;
    float maskS  = texture(u_SelectionTexture, v_TexCoord + vec2( 0.0,     -offset.y)).r;
    float maskE  = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x, 0.0)).r;
    float maskW  = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x, 0.0)).r;
    float maskNE = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x, offset.y)).r;
    float maskNW = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x, offset.y)).r;
    float maskSE = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x,-offset.y)).r;
    float maskSW = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x,-offset.y)).r;

    float edge = maskN + maskS + maskE + maskW + maskNE + maskNW + maskSE + maskSW;

    vec3 outlineColor = vec3(1.0, 0.65, 0.0); 

    if (maskCenter < 0.1 && edge > 0.1) {
        finalColor = outlineColor;
    } else if (maskCenter > 0.5) {
        finalColor = mix(finalColor, outlineColor, 0.15);
    }
    
    FragColor = vec4(finalColor, 1.0);
}