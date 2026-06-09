#version 410 core
layout(location = 0) out vec4 FragColor;

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TexCoord;
in mat3 v_TBN;

uniform vec3 u_CameraPos;

// Material uniforms
uniform vec4 u_Albedo;
uniform float u_Metallic;
uniform float u_Roughness;
uniform float u_AO;
uniform float u_Alpha;
uniform float u_AlphaCutoff;
uniform int u_BlendMode;
uniform int u_UseAlbedoMap;
uniform int u_UseNormalMap;
uniform int u_UseORMMap;
uniform int u_UseMetallicMap;
uniform int u_UseRoughnessMap;
uniform int u_UseAOMap;

uniform sampler2D u_AlbedoMap;
uniform sampler2D u_NormalMap;
uniform sampler2D u_ORMMap;
uniform sampler2D u_MetallicMap;
uniform sampler2D u_RoughnessMap;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 GetNormal() {
    if (u_UseNormalMap == 0) return normalize(v_Normal);
    vec3 tn = texture(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;
    return normalize(v_TBN * tn);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    // Resolve material
    vec3 albedo = u_UseAlbedoMap == 1 ? texture(u_AlbedoMap, v_TexCoord).rgb : u_Albedo.rgb;
    float metallic, roughness, ao;
    if (u_UseORMMap == 1) {
        vec3 orm = texture(u_ORMMap, v_TexCoord).rgb;
        ao = orm.r; roughness = orm.g; metallic = orm.b;
    } else {
        metallic  = u_UseMetallicMap  == 1 ? texture(u_MetallicMap,  v_TexCoord).r : u_Metallic;
        roughness = u_UseRoughnessMap == 1 ? texture(u_RoughnessMap, v_TexCoord).r : u_Roughness;
        ao = u_AO;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic,  0.0, 1.0);

    // Alpha cutoff
    if (u_BlendMode == 1) {
        if (u_Alpha * (u_UseAlbedoMap == 1 ? texture(u_AlbedoMap, v_TexCoord).a : 1.0) < u_AlphaCutoff)
            discard;
    }

    vec3 N = GetNormal();
    vec3 V = normalize(u_CameraPos - v_WorldPos);

    // Ambient
    vec3 ground = vec3(0.04, 0.04, 0.05);
    vec3 sky    = vec3(0.18, 0.20, 0.25);
    vec3 ambient = mix(ground, sky, N.y * 0.5 + 0.5) * 0.8;

    vec3 color = ambient * albedo * ao;

    // Studio key
    {
        vec3 L = normalize(vec3(0.8, 1.0, 0.6));
        vec3 lightCol = vec3(1.0, 0.96, 0.9) * 2.2;
        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 spec = (D * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        color += (kD * albedo / PI + spec) * lightCol * NdotL;
    }

    // Studio fill
    {
        vec3 L = normalize(vec3(-0.8, 0.4, -0.4));
        vec3 lightCol = vec3(0.5, 0.6, 0.8) * 0.8;
        float NdotL = max(dot(N, L), 0.0);
        color += albedo * lightCol * NdotL * (1.0 - metallic);
    }

    // Rim
    {
        vec3 L = normalize(vec3(0.0, 0.2, -1.0));
        vec3 lightCol = vec3(0.9, 0.95, 1.0) * 1.8;
        float NdotL = max(dot(N, L), 0.0);
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.5);
        color += albedo * lightCol * NdotL * rim;
    }

    color = ACESFilm(color);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
