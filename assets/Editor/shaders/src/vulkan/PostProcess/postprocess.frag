#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

// 贴图槽位绑定
layout(set = 1, binding = 0) uniform sampler2D u_ScreenTexture; 
layout(set = 1, binding = 1) uniform sampler2D u_SelectionTexture;
layout(set = 1, binding = 2) uniform sampler2D u_BloomTexture;

// 严格对齐的 Push Constant
layout(push_constant) uniform Constants {
    float Exposure;
    int ToneMappingType;
    vec2 TexelSize;
    int EnableBloom;
    float BloomIntensity;
} pc;

// ACES 电影级色调映射曲线
vec3 ACESFilm(vec3 x) {
    float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec4 hdrData = texture(u_ScreenTexture, v_TexCoord);
    vec3 hdrColor = hdrData.rgb;

    // if (pc.EnableBloom == 1) {
    //     vec3 bloomColor = texture(u_BloomTexture, v_TexCoord).rgb;
    //     hdrColor += bloomColor * pc.BloomIntensity; 
    // }

    // 1. 物理曝光缩放
    // hdrColor *= pc.Exposure;

    vec3 mapped = vec3(0.0);
    mapped = vec3(1.0) - exp(-hdrColor);

    // // 2. 色调映射
    // if (pc.ToneMappingType == 0) {
    //     mapped = vec3(1.0) - exp(-hdrColor);
    // } else {
    //     mapped = ACESFilm(hdrColor);
    // }
    
    // 3. 伽马校正 (sRGB)
    mapped = pow(mapped, vec3(1.0 / 2.2));
    vec3 finalColor = mapped;
    
    // // 4. 描边魔法 (与 OpenGL 保持一致)
    // float maskCenter = texture(u_SelectionTexture, v_TexCoord).r;
    // vec2 offset = pc.TexelSize * 2.0;
    // float maskN  = texture(u_SelectionTexture, v_TexCoord + vec2( 0.0,      offset.y)).r;
    // float maskS  = texture(u_SelectionTexture, v_TexCoord + vec2( 0.0,     -offset.y)).r;
    // float maskE  = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x, 0.0)).r;
    // float maskW  = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x, 0.0)).r;
    // float maskNE = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x, offset.y)).r;
    // float maskNW = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x, offset.y)).r;
    // float maskSE = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x,-offset.y)).r;
    // float maskSW = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x,-offset.y)).r;
    // float edge = maskN + maskS + maskE + maskW + maskNE + maskNW + maskSE + maskSW;
    // vec3 outlineColor = vec3(1.0, 0.65, 0.0); 

    // if (maskCenter < 0.1 && edge > 0.1) {
    //     finalColor = outlineColor;
    // } else if (maskCenter > 0.5) {
    //     finalColor = mix(finalColor, outlineColor, 0.15);
    // }
    
    FragColor = vec4(finalColor.rgb, 1.0);
}