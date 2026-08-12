#version 450 core

layout(set=1, binding=0) uniform sampler2D u_SSRResult;
layout(set=1, binding=1) uniform sampler2D g_PBR;
layout(set=1, binding=2) uniform sampler2D u_DepthMap;

layout(push_constant) uniform PC {
    mat4  u_InverseViewProj;
    vec2  u_BlurDir;
    vec2  u_TexelSize;
    float u_DepthThreshold;
    float _pad;
} pc;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

void main() {
    float roughness   = texture(g_PBR, v_TexCoord).g;
    float centerDepth = texture(u_DepthMap, v_TexCoord).r;

    // Continuous blur radius — no integer truncation banding
    float fRadius   = roughness * 8.0;
    int   maxRadius = int(ceil(fRadius));

    if (maxRadius == 0) {
        FragColor = texture(u_SSRResult, v_TexCoord);
        return;
    }

    // Dynamic Gaussian sigma — scales smoothly with roughness
    float sigma = max(fRadius * 0.5, 0.5);
    float twoSigmaSq = 2.0 * sigma * sigma;

    vec4 accum = vec4(0.0);
    float weightSum = 0.0;

    for (int i = -maxRadius; i <= maxRadius; i++) {
        vec2 uv = v_TexCoord + pc.u_BlurDir * pc.u_TexelSize * float(i);

        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) continue;

        float sampleDepth = texture(u_DepthMap, uv).r;
        if (sampleDepth >= 1.0) continue;

        vec4 sampleSSR = texture(u_SSRResult, uv);

        // Bilateral depth weight
        float depthDiff = abs(centerDepth - sampleDepth);
        float depthWeight = exp(-depthDiff * pc.u_DepthThreshold);

        // Spatial Gaussian with continuous edge fade
        float dist = abs(float(i));
        float edgeFade = clamp(fRadius - dist + 1.0, 0.0, 1.0);
        float spatialWeight = exp(-(dist * dist) / twoSigmaSq) * edgeFade;

        // Premultiplied alpha: color already has alpha baked in, no extra a-weight
        float w = depthWeight * spatialWeight;
        accum += sampleSSR * w;
        weightSum += w;
    }

    FragColor = (weightSum > 0.001) ? (accum / weightSum) : texture(u_SSRResult, v_TexCoord);
}
