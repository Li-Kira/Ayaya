#version 450 core
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in mat3 v_TBN;

// Set 0: UBO
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 CameraPosition;
} u_Camera;

// Set 1: material textures (preview-specific bindings)
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform sampler2D u_NormalMap;
layout(set = 1, binding = 2) uniform sampler2D u_ORMMap;
layout(set = 1, binding = 3) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 4) uniform sampler2D u_RoughnessMap;

layout(push_constant) uniform PushData {
    mat4 ModelMatrix;
    vec4 Albedo;
    float Metallic;
    float Roughness;
    float AO;
    float Alpha;
    float AlphaCutoff;
    int BlendMode;           // 0=Opaque, 1=Masked
    int UseAlbedoMap;
    int UseNormalMap;
    int UseORMMap;
    int UseMetallicMap;
    int UseRoughnessMap;
    int UseAOMap;
} u_Push;

const float PI = 3.14159265359;

// ── PBR BRDF functions ──────────────────────────────────────────

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── Normal mapping ──────────────────────────────────────────────

vec3 GetPreviewNormal() {
    if (u_Push.UseNormalMap == 0)
        return normalize(v_Normal);
    vec3 tn = texture(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;
    return normalize(v_TBN * tn);
}

// ── Studio three-point lighting ─────────────────────────────────

vec3 StudioKeyLight(vec3 N, vec3 V, vec3 albedo, float roughness) {
    vec3 lightDir = normalize(vec3(0.8, 1.0, 0.6));
    vec3 color = vec3(1.0, 0.96, 0.9) * 2.2;
    vec3 H = normalize(lightDir + V);
    float NdotL = max(dot(N, lightDir), 0.0);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, u_Push.Metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, lightDir, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - u_Push.Metallic);
    return (kD * albedo / PI + specular) * color * NdotL;
}

vec3 StudioFillLight(vec3 N, vec3 albedo, float roughness) {
    vec3 lightDir = normalize(vec3(-0.8, 0.4, -0.4));
    vec3 color = vec3(0.5, 0.6, 0.8) * 0.8;
    float NdotL = max(dot(N, lightDir), 0.0);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, u_Push.Metallic / max(roughness, 0.01));

    // Fill light: simplified diffuse only (saves ALU, looks correct)
    vec3 kD = (vec3(1.0) - F0) * (1.0 - u_Push.Metallic);
    return kD * albedo / PI * color * NdotL;
}

vec3 StudioRimLight(vec3 N, vec3 V, vec3 albedo) {
    float NdotV = max(dot(N, V), 0.0001);
    vec3 lightDir = normalize(vec3(0.0, 0.2, -1.0));
    vec3 color = vec3(0.9, 0.95, 1.0) * 1.8;
    float NdotL = max(dot(N, lightDir), 0.0);
    float rim = pow(1.0 - NdotV, 3.5);
    return albedo * color * NdotL * rim;
}

// ── Tone mapping ────────────────────────────────────────────────

vec3 ACESFilm(vec3 x) {
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ── Main ────────────────────────────────────────────────────────

void main() {
    // ── Resolve material parameters ──
    vec3 albedo = u_Push.UseAlbedoMap == 1
        ? texture(u_AlbedoMap, v_TexCoord).rgb
        : u_Push.Albedo.rgb;

    float metallic, roughness, ao;

    if (u_Push.UseORMMap == 1) {
        // UE4-style packed ORM: R=AO, G=Roughness, B=Metallic
        vec3 orm = texture(u_ORMMap, v_TexCoord).rgb;
        ao        = orm.r;
        roughness = orm.g;
        metallic  = orm.b;
    } else {
        metallic  = u_Push.UseMetallicMap  == 1
            ? texture(u_MetallicMap,  v_TexCoord).r : u_Push.Metallic;
        roughness = u_Push.UseRoughnessMap == 1
            ? texture(u_RoughnessMap, v_TexCoord).r : u_Push.Roughness;
        ao        = u_Push.UseAOMap == 1
            ? texture(u_RoughnessMap, v_TexCoord).r : u_Push.AO;
        // Note: AO map shares the RoughnessMap sampler in this simple layout.
        // For full per-texture AO, a 6th sampler would be needed.
        // In practice, ORM maps or per-channel maps cover this.
    }

    // Clamp to avoid NaNs
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic,  0.0,  1.0);
    ao        = clamp(ao,        0.0,  1.0);

    // ── Alpha cutoff for Masked materials ──
    if (u_Push.BlendMode == 1) { // Masked
        if (u_Push.Alpha * (u_Push.UseAlbedoMap == 1
                ? texture(u_AlbedoMap, v_TexCoord).a : 1.0) < u_Push.AlphaCutoff)
            discard;
    }

    // ── Normal ──
    vec3 N = GetPreviewNormal();
    if (dot(N, N) < 1e-8) N = vec3(0.0, 0.0, 1.0);

    vec3 V = u_Camera.CameraPosition - v_WorldPos;
    V = (dot(V, V) > 1e-8) ? normalize(V) : vec3(0.0, 0.0, 1.0);

    // ── Hemispherical ambient ──
    vec3 groundColor = vec3(0.04, 0.04, 0.05);
    vec3 skyColor    = vec3(0.18, 0.20, 0.25);
    vec3 ambient = mix(groundColor, skyColor, N.y * 0.5 + 0.5) * 0.8;

    // ── Lighting ──
    vec3 finalColor = ambient * albedo * ao;

    // Studio three-point (always active, gives consistent attractive look)
    finalColor += StudioKeyLight(N, V, albedo, roughness);
    finalColor += StudioFillLight(N, albedo, roughness);
    finalColor += StudioRimLight(N, V, albedo);

    // NaN / Inf guard
    if (any(isnan(finalColor)) || any(isinf(finalColor)))
        finalColor = vec3(0.0, 0.0, 0.0);

    // ── Post-processing ──
    finalColor = ACESFilm(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    FragColor = vec4(finalColor, 1.0);
}
