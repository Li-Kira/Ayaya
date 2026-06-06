#version 450 core
// WBOIT Resolve Pass — composits Accumulation/Revealage onto SceneColor_HDR
// Blended with standard alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)

layout(location = 0) out vec4 out_Color;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_Accumulation;
layout(set = 1, binding = 1) uniform sampler2D u_Revealage;

// Exposure compensation — matches PostProcess tone-mapping range
layout(push_constant) uniform PC {
    float u_Exposure;
} pc;

void main() {
    float revealage = texture(u_Revealage, v_TexCoord).r;
    if (revealage >= 1.0) discard;

    vec4 accum = texture(u_Accumulation, v_TexCoord);
    if (isinf(accum.r) || isinf(accum.g) || isinf(accum.b))
        accum.rgb = vec3(accum.a);

    vec3 averageColor = accum.rgb / max(accum.a, 1e-5);
    float finalAlpha = 1.0 - revealage;

    // Bring LDR resolve output into the same HDR range as Lighting,
    // so PostProcess tone-mapping handles both consistently
    averageColor *= pc.u_Exposure;

    out_Color = vec4(averageColor, clamp(finalAlpha, 0.0, 1.0));
}
