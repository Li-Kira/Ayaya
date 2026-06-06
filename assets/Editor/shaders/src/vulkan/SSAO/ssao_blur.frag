#version 450 core
layout(location = 0) out float o_AO;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_AO;
layout(set = 1, binding = 1) uniform sampler2D u_Position;
layout(set = 1, binding = 2) uniform sampler2D u_Normal;

layout(push_constant) uniform PC {
    vec2  u_BlurDir;
    vec2  u_TexelSize;
    float u_DepthThreshold;  // world-space distance threshold (meters, ~5.0)
} pc;

void main() {
    vec3 centerPos = texture(u_Position, v_TexCoord).xyz;
    vec3 centerNormal = texture(u_Normal, v_TexCoord).xyz;

    // Background / skybox — no blur, output clean white
    if (length(centerNormal) < 0.1) { o_AO = 1.0; return; }

    float total = 0.0;
    float wsum = 0.0;
    int kernel = 4;

    for (int i = -kernel; i <= kernel; i++) {
        vec2 uv = v_TexCoord + pc.u_BlurDir * pc.u_TexelSize * float(i);

        vec3 p = texture(u_Position, uv).xyz;
        vec3 n = texture(u_Normal, uv).xyz;

        // Bilateral weight: exponential position falloff + sharp normal cutoff
        float dist = distance(centerPos, p);
        float posWeight    = exp(-dist * dist * pc.u_DepthThreshold);
        float normalWeight = pow(max(dot(centerNormal, n), 0.0), 4.0);
        float weight = posWeight * normalWeight;

        total += texture(u_AO, uv).r * weight;
        wsum  += weight;
    }

    o_AO = (wsum > 0.001) ? total / wsum : 1.0;
}
