#version 410 core
layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;
uniform sampler2D u_Image;
uniform vec2 u_TexelSize;
uniform int u_MipLevel;     // 当前处理的 Mip 层级
uniform float u_Threshold;  // 泛光阈值
uniform vec3 u_Curve;       // 软膝曲线参数 (x: threshold - knee, y: knee * 2, z: 0.25 / knee)

// 安全采样：彻底消灭导致红黄色块的 NaN/Inf 病毒像素
vec3 SafeSample(vec2 uv) {
    vec3 c = texture(u_Image, uv).rgb;
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    // 限制单像素最高亮度，防止极端 Fireflies 噪点
    return clamp(c, vec3(0.0), vec3(100.0)); 
}

void main() {
    vec2 uv = v_TexCoord;
    float x = u_TexelSize.x;
    float y = u_TexelSize.y;

    // 13-Tap 采样
    vec3 a = SafeSample(vec2(uv.x - 2.0*x, uv.y + 2.0*y));
    vec3 b = SafeSample(vec2(uv.x,         uv.y + 2.0*y));
    vec3 c = SafeSample(vec2(uv.x + 2.0*x, uv.y + 2.0*y));
    vec3 d = SafeSample(vec2(uv.x - 2.0*x, uv.y));
    vec3 e = SafeSample(vec2(uv.x,         uv.y));
    vec3 f = SafeSample(vec2(uv.x + 2.0*x, uv.y));
    vec3 g = SafeSample(vec2(uv.x - 2.0*x, uv.y - 2.0*y));
    vec3 h = SafeSample(vec2(uv.x,         uv.y - 2.0*y));
    vec3 i = SafeSample(vec2(uv.x + 2.0*x, uv.y - 2.0*y));
    vec3 j = SafeSample(vec2(uv.x - x, uv.y + y));
    vec3 k = SafeSample(vec2(uv.x + x, uv.y + y));
    vec3 l = SafeSample(vec2(uv.x - x, uv.y - y));
    vec3 m = SafeSample(vec2(uv.x + x, uv.y - y));

    // 先进行加权平均混合
    vec3 color = e*0.125 + (a+c+g+i)*0.03125 + (b+d+f+h)*0.0625 + (j+k+l+m)*0.125;

    // 核心修复：在第一层精确执行阈值截断 (Prefilter)
    if (u_MipLevel == 0) {
        float brightness = max(max(color.r, color.g), color.b);
        float rq = clamp(brightness - u_Curve.x, 0.0, u_Curve.y);
        rq = u_Curve.z * rq * rq;
        float factor = max(rq, brightness - u_Threshold) / max(brightness, 0.0001);
        color *= factor;
    }

    o_Color = vec4(color, 1.0);
}