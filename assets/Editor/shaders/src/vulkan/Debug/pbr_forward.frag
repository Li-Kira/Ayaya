#version 450 core
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

// Set 1: 材质纹理
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 2) uniform samplerCube u_PrefilterMap;
layout(set = 1, binding = 3) uniform sampler2D u_BRDFLUT;
layout(set = 1, binding = 4) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 5) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 6) uniform sampler2D u_AOMap;
layout(set = 1, binding = 7) uniform sampler2D u_NormalMap;

// Set 0: 引擎 UBO
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 ViewPos;
} u_Camera;

struct PointLight {
    vec4 Position;  // xyz = pos, w = radius
    vec4 Color;     // rgb * candelas, w = falloff
};

layout(set = 0, binding = 1) uniform LightData {
    vec4 DirLightDir;   // xyz = direction
    vec4 DirLightColor; // xyz = color * illuminance
    PointLight PointLights[4];
    int PointLightCount;
} u_Light;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec4 Albedo;                   // offset 64
    int UseAlbedoMap;              // offset 80
    float Metallic;                // offset 84
    float Roughness;               // offset 88
    float AO;                      // offset 92
    int UseMetallicMap;            // offset 96
    int UseRoughnessMap;           // offset 100
    int UseAOMap;                  // offset 104
    int UseNormalMap;              // offset 108
    float EnvironmentIntensity;    // offset 112
    float _pad0;                   // offset 116
    float _pad1;                   // offset 120
    float _pad2;                   // offset 124
    vec4 EnvironmentAmbientColor;  // offset 128
} u_Push;

const float PI = 3.14159265359;

// ==========================================
// PBR 核心数学 (Cook-Torrance BRDF)
// ==========================================
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
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 GetNormalFromMap(vec3 worldNormal, vec3 worldPos, vec2 texCoord) {
    vec3 tangentNormal = texture(u_NormalMap, texCoord).xyz * 2.0 - 1.0;
    vec3 Q1  = dFdx(worldPos);
    vec3 Q2  = dFdy(worldPos);
    vec2 st1 = dFdx(texCoord);
    vec2 st2 = dFdy(texCoord);
    vec3 N   = normalize(worldNormal);
    vec3 T   = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B   = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main() {
    // ---- 材质采样 ----
    vec3 albedo = u_Push.UseAlbedoMap == 1
        ? texture(u_AlbedoMap, v_TexCoord).rgb
        : u_Push.Albedo.rgb;

    float metallic = u_Push.UseMetallicMap == 1
        ? texture(u_MetallicMap, v_TexCoord).r
        : u_Push.Metallic;
    float roughness = u_Push.UseRoughnessMap == 1
        ? texture(u_RoughnessMap, v_TexCoord).r
        : u_Push.Roughness;
    float ao = u_Push.UseAOMap == 1
        ? texture(u_AOMap, v_TexCoord).r
        : u_Push.AO;

    vec3 N = u_Push.UseNormalMap == 1
        ? GetNormalFromMap(v_Normal, v_WorldPos, v_TexCoord)
        : normalize(v_Normal);
    vec3 V = normalize(u_Camera.ViewPos - v_WorldPos);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // ---- 直接光照 Lo ----
    vec3 Lo = vec3(0.0);

    // 方向光 (Directional Light)
    {
        vec3 L = normalize(-u_Light.DirLightDir.xyz);
        vec3 H = normalize(V + L);
        vec3 radiance = u_Light.DirLightColor.rgb;

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kS_dir = F;
        vec3 kD_dir = (vec3(1.0) - kS_dir) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);

        Lo += (kD_dir * albedo / PI + specular) * radiance * NdotL;
    }

    // 点光源 (Point Lights)
    for (int i = 0; i < u_Light.PointLightCount && i < 4; i++) {
        vec3 lightPos = u_Light.PointLights[i].Position.xyz;
        float radius = u_Light.PointLights[i].Position.w;
        vec3 lightColor = u_Light.PointLights[i].Color.rgb;
        float falloff = u_Light.PointLights[i].Color.w;

        vec3 L_point = normalize(lightPos - v_WorldPos);
        vec3 H_point = normalize(V + L_point);
        float distance = length(lightPos - v_WorldPos);

        float attenuation = 1.0 / (distance * distance + 0.0001);
        float distByRadius = distance / radius;
        float windowing = clamp(1.0 - pow(distByRadius, 4.0), 0.0, 1.0);
        windowing = pow(windowing, falloff + 1.0);
        attenuation *= windowing;

        vec3 radiance_point = lightColor * attenuation;

        float NDF_p = DistributionGGX(N, H_point, roughness);
        float G_p   = GeometrySmith(N, V, L_point, roughness);
        vec3  F_p   = fresnelSchlick(max(dot(H_point, V), 0.0), F0);

        vec3 numerator_p    = NDF_p * G_p * F_p;
        float denominator_p = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L_point), 0.0) + 0.0001;
        vec3 specular_p = numerator_p / denominator_p;

        vec3 kS_p = F_p;
        vec3 kD_p = (vec3(1.0) - kS_p) * (1.0 - metallic);
        float NdotL_p = max(dot(N, L_point), 0.0);

        Lo += (kD_p * albedo / PI + specular_p) * radiance_point * NdotL_p;
    }

    // ---- IBL 环境光 (Split-Sum Approximation) ----
    float NdotV = max(dot(N, V), 0.0);
    vec3 F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);

    vec3 irradiance = u_Push.EnvironmentAmbientColor.rgb;
    irradiance += texture(u_IrradianceMap, N).rgb * u_Push.EnvironmentIntensity;
    vec3 diffuse = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb * u_Push.EnvironmentIntensity;
    vec2 brdf = texture(u_BRDFLUT, vec2(max(NdotV, 1e-5), roughness)).rg;
    vec3 specular_ibl = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    vec3 ambient = (kD_ibl * diffuse + specular_ibl) * ao;

    // ---- 最终合成 ----
    vec3 color = ambient + Lo;
    FragColor = vec4(color, 1.0);
}
