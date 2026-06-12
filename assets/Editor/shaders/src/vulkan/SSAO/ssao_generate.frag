#version 450 core
layout(location = 0) out float o_AO;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 0, binding = 0) uniform Camera { mat4 u_ViewProjection; mat4 u_View; vec3 u_CameraPosition; };
layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 1) uniform sampler2D g_Normal;
layout(set = 1, binding = 2) uniform sampler2D u_Noise;

layout(push_constant) uniform PC {
    mat4  u_InverseViewProj;
    vec2  u_NoiseScale; float u_Radius; float u_Bias; float u_Power; int u_SampleCount; int _pad;
} pc;

vec3 WorldPosFromDepth(float d, vec2 uv) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, d, 1.0);
    vec4 wp = pc.u_InverseViewProj * ndc;
    return wp.xyz / wp.w;
}

void main() {
    float depth = texture(u_DepthMap, v_TexCoord).r;
    if (depth >= 1.0) { o_AO = 1.0; return; }
    vec3 worldPos = WorldPosFromDepth(depth, v_TexCoord);
    vec3 normal = texture(g_Normal, v_TexCoord).xyz;
    if (length(normal) < 0.1) { o_AO = 1.0; return; }
    normal = normalize(normal);

    vec3 rvec = texture(u_Noise, v_TexCoord * pc.u_NoiseScale).xyz * 2.0 - 1.0;
    vec3 T = normalize(rvec - normal * dot(rvec, normal));
    vec3 B = cross(normal, T);
    mat3 TBN = mat3(T, B, normal);

    float occ = 0.0;
    for (int i = 0; i < 64; i++) {
        if (i >= pc.u_SampleCount) break;
        vec3 so = TBN * vec3(sin(i*12.9898)*0.5+sin(i*45.164)*0.5, cos(i*37.719)*0.5+cos(i*93.145)*0.5, (i+1.0)/pc.u_SampleCount*0.5+0.5);
        so = normalize(so) * (i/float(pc.u_SampleCount)*0.5+0.5);
        vec3 swp = worldPos + so * pc.u_Radius;
        vec4 cp = u_ViewProjection * vec4(swp, 1.0); cp.xyz /= cp.w;
        vec2 suv; suv.x = cp.x*0.5+0.5; suv.y = cp.y*(-0.5)+0.5;
        if (suv.x<0.0||suv.x>1.0||suv.y<0.0||suv.y>1.0) continue;
        float od = texture(u_DepthMap, suv).r;
        if (od >= 1.0) continue;
        vec3 op = WorldPosFromDepth(od, suv);
        float ds = distance(u_CameraPosition, swp);
        float dO = distance(u_CameraPosition, op);
        float rc = smoothstep(0.0, 1.0, pc.u_Radius / distance(worldPos, op));
        if (dO < ds - pc.u_Bias) occ += 1.0 * rc;
    }
    o_AO = pow(1.0 - occ / float(pc.u_SampleCount), pc.u_Power);
}
