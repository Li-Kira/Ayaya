#version 450 core

layout(set=1, binding=0) uniform sampler2D u_SSRResult;
layout(set=1, binding=1) uniform sampler2D g_Albedo;
layout(set=1, binding=2) uniform sampler2D g_Normal;
layout(set=1, binding=3) uniform sampler2D g_PBR;
layout(set=1, binding=4) uniform sampler2D u_DepthMap;
layout(set=1, binding=5) uniform sampler2D u_SSAO;
layout(set=1, binding=6) uniform samplerCube u_PrefilteredMap;
layout(set=1, binding=7) uniform sampler2D u_BRDFLUT;

layout(push_constant) uniform PC {
    mat4  u_InverseViewProj;
    vec3  u_CameraPosition;
    float u_EnvIntensity;
    float u_RoughnessStart;
    float u_RoughnessEnd;
    float u_SSRIntensity;
} pc;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

// Octahedral normal decode (identical to deferred_lighting.frag)
vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// Roughness-aware Fresnel-Schlick (identical to deferred_lighting.frag)
vec3 F_SchlickR(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // ── World position reconstruction from SceneDepth ──
    float depth = texture(u_DepthMap, v_TexCoord).r;
    if (depth >= 1.0) discard;
    // Vulkan negative viewport Y-flip: matches deferred_lighting.frag:95
    vec4 ndc = vec4(v_TexCoord.x * 2.0 - 1.0, 1.0 - v_TexCoord.y * 2.0, depth, 1.0);
    vec4 wp = pc.u_InverseViewProj * ndc;
    vec3 worldPos = wp.xyz / wp.w;

    // ── GBuffer sampling ──
    vec3 N = OctDecode(texture(g_Normal, v_TexCoord).rg);
    vec3 albedo = texture(g_Albedo, v_TexCoord).rgb;
    float roughness = texture(g_PBR, v_TexCoord).g;
    float metallic  = texture(g_PBR, v_TexCoord).r;
    float ao        = texture(g_PBR, v_TexCoord).b;
    float ssao      = texture(u_SSAO, v_TexCoord).r;

    vec3 V = normalize(pc.u_CameraPosition - worldPos);
    float NdotV = max(dot(N, V), 0.0);
    vec3 R = reflect(-V, N);

    // === 1. BRDF integration terms (identical to deferred_lighting.frag) ===
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = F_SchlickR(NdotV, F0, roughness);
    vec2 brdf = texture(u_BRDFLUT, vec2(max(NdotV, 1e-5), roughness)).rg;
    vec3 specularBRDF = F * brdf.x + brdf.y;

    // === 2. Raw reflection sources (pure light, no BRDF applied yet) ===
    vec3 iblColor = textureLod(u_PrefilteredMap, R, roughness * 4.0).rgb * pc.u_EnvIntensity;
    vec4 ssr = texture(u_SSRResult, v_TexCoord); // ssr.rgb = pure reflected color

    // === 3. SSR blend weight ===
    // No metalGate — dielectrics also have specular reflections at grazing angles (Fresnel).
    // ssr.a encodes: edgeFade * max(fresnel, 0.1) * metallic * hitFound
    // The march pass discards metallic<0.02, so dielectrics get ssr.a=0 (→ IBL fallback).
    // Future: remove discard, let Fresnel control reflection strength for all surfaces.
    float roughnessFactor = 1.0 - smoothstep(0.2, 0.6, roughness);
    float ssrWeight = ssr.a * roughnessFactor;

    // === 4. Hierarchical replacement — premultiplied alpha additive blend ===
    // ssr.rgb is premultiplied: hitColor * alpha. Hardware bilinear preserves energy.
    // SSR hit  → replace IBL cubemap sample with screen-space reflection
    // SSR miss → fallback to PrefilteredMap cubemap
    float ssrIntensity = pc.u_SSRIntensity;
    vec3 reflectionLight = ssr.rgb * (roughnessFactor * ssrIntensity)
                         + iblColor * (1.0 - ssrWeight * ssrIntensity);

    // === 5. Apply BRDF + occlusion uniformly ===
    // Both SSR and IBL receive the same Fresnel, geometry, and occlusion modulation.
    vec3 finalSpecular = reflectionLight * specularBRDF * ao * ssao;

    // Additive blend (One/One) onto Lighting_NoSpecIBL
    FragColor = vec4(finalSpecular, 1.0);
}
