#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) flat in uint v_LightIndex;

// Set 0: Camera UBO
layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;
    vec3 u_CameraPosition;
};

// Set 1: GBuffer + SceneDepth
layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 1) uniform sampler2D g_Albedo;
layout(set = 1, binding = 2) uniform sampler2D g_PBR;
layout(set = 1, binding = 3) uniform sampler2D g_CustomData;
layout(set = 1, binding = 4) uniform sampler2D g_Normal;

// Set 2: SSBOs
struct SpotLight {
    vec4 positionAndRadius;      // xyz=world pos, w=radius
    vec4 colorAndFalloff;        // rgb=color*candelas, w=falloff
    vec4 directionAndConeAngles; // xyz=cone direction, w=innerConeCos
    vec4 outerConeAndPad;        // x=outerConeCos, yzw=unused (vec4 required for MoltenVK compatibility)
};
layout(std430, set = 2, binding = 0) readonly buffer InstanceSSBO {
    mat4 u_InstanceWorld[];
};
layout(std430, set = 2, binding = 1) readonly buffer SpotLightSSBO {
    uint u_SpotLightCount;
    SpotLight u_SpotLights[];
};

layout(push_constant) uniform PC {
    mat4 u_InverseViewProj;
    vec4 u_ScreenParams;   // x=1/w, y=1/h, z=w, w=h
} pc;

// Octahedral decode
vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

const float PI = 3.14159265359;
float D_GGX(vec3 N, vec3 H, float r) {
    float a = r * r, a2 = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float G_Schlick(float NdV, float r) {
    float k = (r + 1.0) * (r + 1.0) / 8.0;
    return NdV / (NdV * (1.0 - k) + k);
}
float G_Smith(vec3 N, vec3 V, vec3 L, float r) {
    return G_Schlick(max(dot(N, V), 0.0), r) * G_Schlick(max(dot(N, L), 0.0), r);
}
vec3 F_Schlick(float c, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0);
}

void main() {
    SpotLight light = u_SpotLights[v_LightIndex];

    // GBuffer: texelFetch
    ivec2 fc = ivec2(gl_FragCoord.xy);
    float depth     = texelFetch(u_DepthMap,   fc, 0).r;
    vec4 albedoData = texelFetch(g_Albedo,     fc, 0);
    vec4 pbrData    = texelFetch(g_PBR,        fc, 0);
    vec4 customData = texelFetch(g_CustomData, fc, 0);
    vec2 octNormal  = texelFetch(g_Normal,     fc, 0).rg;

    vec3 albedo   = albedoData.rgb;
    float roughness = max(pbrData.g, 0.04);
    float metallic  = pbrData.r;
    float ao        = pbrData.b;
    vec3 N = OctDecode(octNormal);

    // World position reconstruction
    vec2 screenUV = (gl_FragCoord.xy * pc.u_ScreenParams.xy);
    vec4 ws = pc.u_InverseViewProj * vec4(
        screenUV.x * 2.0 - 1.0,
        1.0 - screenUV.y * 2.0,
        depth, 1.0);
    vec3 FragPos = ws.xyz / ws.w;

    // Distance cull (same as point light)
    vec3 toLight = light.positionAndRadius.xyz - FragPos;
    float d2 = dot(toLight, toLight);
    float lr = light.positionAndRadius.w;
    if (d2 > lr * lr) discard;
    float d = sqrt(d2);

    vec3 L = toLight / d;
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) discard;

    // Cone attenuation — smoothstep between outer and inner cone cosines
    float cosTheta = dot(L, -light.directionAndConeAngles.xyz); // L points toward light, coneDir points away
    float spotAtten = smoothstep(light.outerConeAndPad.x, light.directionAndConeAngles.w, cosTheta);

    // Attenuation: inverse-square + polynomial window
    float att = 1.0 / (d * d + 0.0001);
    float dbr = d / lr;
    float w = pow(clamp(1.0 - dbr * dbr * dbr * dbr, 0.0, 1.0),
                  light.colorAndFalloff.w + 1.0);
    vec3 rad = light.colorAndFalloff.rgb * att * w * spotAtten;

    // PBR BRDF
    vec3 V = normalize(u_CameraPosition - FragPos);
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = D_GGX(N, H, roughness);
    float G = G_Smith(N, V, L, roughness);
    vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);

    FragColor = vec4((kD * albedo / PI + spec) * rad * NdotL * ao, 1.0);
    // Additive blend via pipeline state (src=ONE, dst=ONE)
}
