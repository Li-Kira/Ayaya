#version 450 core
layout(location = 0) out float o_AO;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_AO;
layout(set = 1, binding = 1) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 2) uniform sampler2D g_Normal;

layout(push_constant) uniform PC {
    mat4  u_InverseViewProj;
    vec2  u_BlurDir; vec2  u_TexelSize; float u_DepthThreshold; float _pad;
} pc;

vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

vec3 WorldPosFromDepth(float d, vec2 uv) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    vec4 wp = pc.u_InverseViewProj * ndc;
    return wp.xyz / wp.w;
}

void main() {
    float depth = texture(u_DepthMap, v_TexCoord).r;
    if (depth >= 1.0) { o_AO = 1.0; return; }
    vec3 cp = WorldPosFromDepth(depth, v_TexCoord);
    vec3 cn = OctDecode(texture(g_Normal, v_TexCoord).rg);
    if (length(cn) < 0.1) { o_AO = 1.0; return; }

    float total = 0.0, wsum = 0.0;
    for (int i = -4; i <= 4; i++) {
        vec2 uv = v_TexCoord + pc.u_BlurDir * pc.u_TexelSize * float(i);
        float d = texture(u_DepthMap, uv).r;
        if (d >= 1.0) continue;
        vec3 p = WorldPosFromDepth(d, uv);
        vec3 n = OctDecode(texture(g_Normal, uv).rg);
        float dist = distance(cp, p);
        float pw = exp(-dist * dist * pc.u_DepthThreshold);
        float nw = pow(max(dot(cn, n), 0.0), 4.0);
        float w = pw * nw;
        total += texture(u_AO, uv).r * w;
        wsum += w;
    }
    o_AO = (wsum > 0.001) ? total / wsum : 1.0;
}
