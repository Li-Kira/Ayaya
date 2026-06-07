#version 450 core
// WBOIT Resolve Pass — composites Accumulation/Revealage onto SceneColor_HDR
// Blended with standard alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
//
// Outputs raw HDR (same range as deferred Lighting pass). PostProcess
// handles exposure + tone-mapping uniformly for both opaque and transparent.

layout(location = 0) out vec4 out_Color;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_Accumulation;
layout(set = 1, binding = 1) uniform sampler2D u_Revealage;

// Must match the value in wboit_gather.frag
const float WBOIT_PRE_EXPOSURE = 0.01;

void main() {
    float revealage = texture(u_Revealage, v_TexCoord).r;
    if (revealage >= 1.0) discard;

    vec4 accum = texture(u_Accumulation, v_TexCoord);
    if (isinf(accum.r) || isinf(accum.g) || isinf(accum.b))
        accum.rgb = vec3(accum.a);

    // Recover HDR litColor:  accum.rgb = Σ(litColor_i * α_i * w_i * PRE), accum.a = Σ(α_i * w_i)
    vec3 averageColor = (accum.rgb / max(accum.a, 1e-5)) / WBOIT_PRE_EXPOSURE;
    float finalAlpha = 1.0 - revealage;

    out_Color = vec4(averageColor, clamp(finalAlpha, 0.0, 1.0));
}
