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

// Soft-knee threshold prefilter (UE-style). Applied to each input sample BEFORE the
// 13-tap blur, so bright pixels are extracted at full-resolution strength. Applying it
// AFTER the average (the old order) averaged a multi-pixel highlight down below the
// threshold and made bloom far too weak.
vec3 Prefilter(vec3 c) {
    float brightness = max(max(c.r, c.g), c.b);
    float rq = clamp(brightness - pc.u_Curve.x, 0.0, pc.u_Curve.y);
    rq = pc.u_Curve.z * rq * rq;
    float factor = max(rq, brightness - pc.u_Threshold) / max(brightness, 0.0001);
    return c * factor;
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

    // Threshold prefilter at mip 0, applied PER-SAMPLE before the blur (UE-style:
    // "extract brights first, then blur"). Dim pixels are zeroed here; bright pixels
    // keep their full-res strength into the fixed-weight 13-tap average below.
    if (pc.u_MipLevel == 0) {
        a = Prefilter(a); b = Prefilter(b); c = Prefilter(c); d = Prefilter(d);
        e = Prefilter(e); f = Prefilter(f); g = Prefilter(g); h = Prefilter(h);
        i = Prefilter(i); j = Prefilter(j); k = Prefilter(k); l = Prefilter(l);
        m = Prefilter(m);
    }

    // Fixed-weight 13-tap downsample. A plain weighted average keeps multi-pixel
    // highlights bright enough to bloom, whereas Karis (weight = spatial/(1+luminance))
    // over-suppressed them. Firefly suppression is handled by (a) SafeSample clamp,
    // (b) this average dimming single-pixel spikes, and (c) specular AA.
    vec3 color = e*0.125 + (a+c+g+i)*0.03125 + (b+d+f+h)*0.0625 + (j+k+l+m)*0.125;

    o_Color = vec4(color, 1.0);
}