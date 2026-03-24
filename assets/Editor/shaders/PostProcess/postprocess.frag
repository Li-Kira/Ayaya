#version 410 core
out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_ScreenTexture; 
uniform sampler2D u_SelectionTexture;
uniform float u_Exposure = 1.0;   
uniform vec2 u_TexelSize; 

// ACES ToneMapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// 封装一个安全的采样函数：采样 HDR -> 曝光 -> 压成 LDR
vec3 SampleLDR(vec2 uv) {
    vec3 hdr = texture(u_ScreenTexture, uv).rgb;
    return ACESFilm(hdr * u_Exposure);
}

float RGB2Luma(vec3 rgb) {
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main() {
    // 读取带有 Alpha 的完整数据
    vec4 hdrData = texture(u_ScreenTexture, v_TexCoord);
    
    // 仅对 RGB 通道进行色调映射和 Gamma 校正
    vec3 mapped = vec3(1.0) - exp(-hdrData.rgb * u_Exposure);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    vec3 finalColor = mapped;
    
    // 核心：原样输出 Alpha 通道！(1.0 为物理实体，0.0 为虚空背景)
    // FragColor = vec4(mapped, hdrData.a);

    // float FXAA_SPAN_MAX = 8.0;
    // float FXAA_REDUCE_MUL = 1.0/8.0;
    // float FXAA_REDUCE_MIN = 1.0/128.0;

    // // 使用我们自己封装的 SampleLDR 函数获取周围像素
    // vec3 rgbNW = SampleLDR(v_TexCoord + vec2(-1.0, -1.0) * u_TexelSize);
    // vec3 rgbNE = SampleLDR(v_TexCoord + vec2(1.0, -1.0) * u_TexelSize);
    // vec3 rgbSW = SampleLDR(v_TexCoord + vec2(-1.0, 1.0) * u_TexelSize);
    // vec3 rgbSE = SampleLDR(v_TexCoord + vec2(1.0, 1.0) * u_TexelSize);
    // vec3 rgbM  = SampleLDR(v_TexCoord);

    // float lumaNW = RGB2Luma(rgbNW);
    // float lumaNE = RGB2Luma(rgbNE);
    // float lumaSW = RGB2Luma(rgbSW);
    // float lumaSE = RGB2Luma(rgbSE);
    // float lumaM  = RGB2Luma(rgbM);

    // float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    // float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // vec2 dir;
    // dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    // dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    // float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    // float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    
    // dir = min(vec2( FXAA_SPAN_MAX,  FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * u_TexelSize;

    // vec3 rgbA = (1.0/2.0) * (SampleLDR(v_TexCoord + dir * (1.0/3.0 - 0.5)) + SampleLDR(v_TexCoord + dir * (2.0/3.0 - 0.5)));
    // vec3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (SampleLDR(v_TexCoord + dir * (0.0/3.0 - 0.5)) + SampleLDR(v_TexCoord + dir * (3.0/3.0 - 0.5)));
        
    // float lumaB = RGB2Luma(rgbB);

    // if((lumaB < lumaMin) || (lumaB > lumaMax)) {
    //     FragColor = vec4(rgbA, 1.0); // 应用 FXAA 结果 A
    // } else {
    //     FragColor = vec4(rgbB, 1.0); // 应用 FXAA 结果 B
    // }

    // ==========================================
    // 屏幕空间完美描边魔法 (Sobel / Edge Detection)
    // ==========================================
    // 1. 获取当前像素是不是在物体内部 (1.0 = 内部, 0.0 = 外部)
    float maskCenter = texture(u_SelectionTexture, v_TexCoord).r;

    // 2. 采样周围一圈的像素 (乘以 2.0 代表描边有 2 个像素那么粗，你可以自由调整)
    vec2 offset = u_TexelSize * 2.0;
    float maskN  = texture(u_SelectionTexture, v_TexCoord + vec2( 0.0,      offset.y)).r;
    float maskS  = texture(u_SelectionTexture, v_TexCoord + vec2( 0.0,     -offset.y)).r;
    float maskE  = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x, 0.0)).r;
    float maskW  = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x, 0.0)).r;
    float maskNE = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x, offset.y)).r;
    float maskNW = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x, offset.y)).r;
    float maskSE = texture(u_SelectionTexture, v_TexCoord + vec2( offset.x,-offset.y)).r;
    float maskSW = texture(u_SelectionTexture, v_TexCoord + vec2(-offset.x,-offset.y)).r;

    // 3. 周围有任何一点白，edge 就会大于 0
    float edge = maskN + maskS + maskE + maskW + maskNE + maskNW + maskSE + maskSW;

    vec3 outlineColor = vec3(1.0, 0.65, 0.0); // 描边颜色：Ayaya 橙黄色

    // 核心判定：如果我当前像素是黑的 (不在物体上)，但我周围摸到了白色 (碰到了物体边缘) -> 我就是轮廓线！
    if (maskCenter < 0.1 && edge > 0.1) {
        finalColor = outlineColor;
    } 
    // 锦上添花：给被选中的物体表面叠加一层淡淡的橙色 (X-Ray 透视高亮效果)
    else if (maskCenter > 0.5) {
        finalColor = mix(finalColor, outlineColor, 0.15);
    }
    FragColor = vec4(finalColor, 1.0);
}