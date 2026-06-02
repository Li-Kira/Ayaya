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
layout(set = 1, binding = 8) uniform sampler2D u_ShadowMap;

// Set 0: 引擎 UBO
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 ViewPos;
} u_Camera;

struct PointLight {
    vec4 Position;
    vec4 Color;
};

layout(set = 0, binding = 1) uniform LightData {
    vec4 DirLightDir;
    vec4 DirLightColor;
    PointLight PointLights[4];
    int PointLightCount;
} u_Light;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec4 Albedo;
    int UseAlbedoMap;
    float Metallic;
    float Roughness;
    float AO;
    int UseMetallicMap;
    int UseRoughnessMap;
    int UseAOMap;
    int UseNormalMap;
    float EnvironmentIntensity;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4 EnvironmentAmbientColor;
    mat4 LightSpaceMatrix;
    int EnableShadows;
} u_Push;

const float PI = 3.14159265359;

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
    vec3 N = normalize(worldNormal);
    vec3 T = Q1 * st2.t - Q2 * st1.t;
    T = (dot(T,T) > 1e-8) ? normalize(T) : vec3(1.0, 0.0, 0.0);
    vec3 Bt = cross(N, T);
    Bt = (dot(Bt,Bt) > 1e-8) ? normalize(Bt) : cross(N, vec3(0.0, 1.0, 0.0));
    vec3 B = -Bt;
    mat3 TBN = mat3(T, B, N);
    vec3 nm = TBN * tangentNormal;
    return (dot(nm,nm) > 1e-8) ? normalize(nm) : N;
}

float ShadowCalculation(vec4 fragPosLightSpace, float NdotL) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * (-0.5) + 0.5; // Vulkan viewport Y-flip compensation
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    float bias = max(0.001 * (1.0 - NdotL), 0.0001);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
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
    if (dot(N,N) < 1e-8) N = vec3(0.0, 0.0, 1.0);

    vec3 V = u_Camera.ViewPos - v_WorldPos;
    V = (dot(V,V) > 1e-8) ? normalize(V) : vec3(0.0, 0.0, 1.0);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);

    // 方向光
    {
        vec3 Ld = -u_Light.DirLightDir.xyz;
        vec3 L = (dot(Ld,Ld) > 1e-8) ? normalize(Ld) : vec3(0.0, 0.0, 1.0);
        vec3 Hv = V + L;
        vec3 H = (dot(Hv,Hv) > 1e-8) ? normalize(Hv) : vec3(0.0, 0.0, 1.0);
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

        float shadowFactor = 1.0;
        if (u_Push.EnableShadows == 1) {
            vec4 fragPosLightSpace = u_Push.LightSpaceMatrix * vec4(v_WorldPos, 1.0);
            shadowFactor = 1.0 - ShadowCalculation(fragPosLightSpace, NdotL);
        }
        Lo += (kD_dir * albedo / PI + specular) * radiance * NdotL * shadowFactor;
    }

    // 点光源
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

    // IBL
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

    vec3 color = ambient + Lo;
    if (any(isnan(color)) || any(isinf(color))) color = vec3(0.0, 0.0, 0.0);
    FragColor = vec4(color, 1.0);
}
