#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 0) in vec2 v_TexCoord;

// 明确绑定贴图槽位
layout(set = 1, binding = 0) uniform sampler2D u_Image;

// 【核心改造】：将散装的 Uniform 统合进 Push Constant
layout(push_constant) uniform Constants {
    vec2  u_TexelSize;
    int   u_MipLevel;
    float u_Threshold;
    vec3  u_Curve;
} pc;

vec3 SafeSample(vec2 uv) {
    vec3 c = texture(u_Image, uv).rgb;
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(100.0)); 
}

void main() {
    vec2 uv = v_TexCoord;
    float x = pc.u_TexelSize.x;
    float y = pc.u_TexelSize.y;

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

    vec3 color = e*0.125 + (a+c+g+i)*0.03125 + (b+d+f+h)*0.0625 + (j+k+l+m)*0.125;

    // 核心修复：在第一层精确执行阈值截断 (Prefilter)
    if (pc.u_MipLevel == 0) {
        float brightness = max(max(color.r, color.g), color.b);
        float rq = clamp(brightness - pc.u_Curve.x, 0.0, pc.u_Curve.y);
        rq = pc.u_Curve.z * rq * rq;
        float factor = max(rq, brightness - pc.u_Threshold) / max(brightness, 0.0001);
        color *= factor;
    }

    o_Color = vec4(color, 1.0);
}