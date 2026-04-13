#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

// ==========================================
// 1. 贴图采样器：显式分配 Binding 槽位
// ==========================================
layout(binding = 0) uniform sampler2D u_ScreenTexture; 
layout(binding = 1) uniform sampler2D u_SelectionTexture;
layout(binding = 2) uniform sampler2D u_BloomTexture;

// ==========================================
// 2. 散装 Uniform 统合：Push Constant
// ==========================================
layout(push_constant) uniform Constants {
    float u_Exposure;        // 4 Bytes
    float _padding1;         // 4 Bytes (必须补齐 8 字节边界，因为下面的 vec2 要求 8 字节对齐)
    vec2  u_TexelSize;       // 8 Bytes
    int   u_ToneMappingType; // 4 Bytes (0 = Reinhard, 1 = ACES)
    int   u_EnableBloom;     // 4 Bytes (将 bool 替换为 int 保证跨显卡内存对齐安全)
    float u_BloomIntensity;  // 4 Bytes
} pc;

// ACES 电影级色调映射曲线
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    // 读取带有 Alpha 的完整数据
    vec4 hdrData = texture(u_ScreenTexture, v_TexCoord);
    vec3 hdrColor = hdrData.rgb;

    // 【修改】：使用 pc. 访问，并用 == 1 判断布尔逻辑
    if (pc.u_EnableBloom == 1) {
        vec3 bloomColor = texture(u_BloomTexture, v_TexCoord).rgb;
        hdrColor += bloomColor * pc.u_BloomIntensity; // 加法混合
    }

    // 1. 物理曝光缩放
    hdrColor *= pc.u_Exposure;
    vec3 mapped = vec3(0.0);
    
    // 2. 色调映射 (Tone Mapping)
    if (pc.u_ToneMappingType == 0) {
        mapped = vec3(1.0) - exp(-hdrColor);
    } else {
        mapped = ACESFilm(hdrColor);
    }
    
    // 3. 伽马校正 (Gamma Correction)
    mapped = pow(mapped, vec3(1.0 / 2.2));
    vec3 finalColor = mapped;
    
    // ==========================================
    // 屏幕空间完美描边魔法
    // ==========================================
    float maskCenter = texture(u_SelectionTexture, v_TexCoord).r;
    
    // 【修改】：使用 pc.u_TexelSize
    vec2 offset = pc.u_TexelSize * 2.0;
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