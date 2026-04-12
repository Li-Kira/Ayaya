#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

// ==========================================
// 贴图采样器 (放在 Set 1 对应贴图槽位)
// ==========================================
layout(set = 1, binding = 0) uniform sampler2D g_Position;
layout(set = 1, binding = 1) uniform sampler2D g_Normal;
layout(set = 1, binding = 2) uniform sampler2D g_Albedo;
layout(set = 1, binding = 3) uniform sampler2D g_PBR; 
layout(set = 1, binding = 4) uniform sampler2D g_CustomData;

layout(set = 1, binding = 5) uniform sampler2D u_ShadowMap;
layout(set = 1, binding = 8) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 9) uniform samplerCube u_PrefilteredMap;
layout(set = 1, binding = 10) uniform sampler2D u_BRDFLUT;

// ==========================================
// 引擎全局 UBO (放在 Set 0)
// ==========================================
layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

struct PointLight {
    vec4 Position;
    vec4 Color;
};

layout(set = 0, binding = 1) uniform LightData {
    vec4 DirLightDir;
    vec4 DirLightColor;
    PointLight PointLights[4];
    int PointLightCount;
};

// ==========================================
// Push Constants (散装 Uniform 统合)
// ==========================================
layout(push_constant) uniform Constants {
    mat4 u_LightSpaceMatrix;
    vec3 u_AmbientColor;
    float u_Intensity;
    int u_EnvMapEnabled;
} pc;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if(projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }    
    }
    shadow /= 9.0;
    return shadow;
}

void main() {
    vec4 posData = texture(g_Position, v_TexCoord);
    if (posData.a < 0.1) discard;

    vec3 FragPos = posData.rgb;
    vec3 Normal = texture(g_Normal, v_TexCoord).rgb;
    vec3 Albedo = texture(g_Albedo, v_TexCoord).rgb;
    
    vec4 pbrData = texture(g_PBR, v_TexCoord);
    float Metallic = pbrData.r;
    float Roughness = pbrData.g;
    float AO = pbrData.b;

    float ReceiveShadows = texture(g_CustomData, v_TexCoord).r;

    vec3 N = normalize(Normal);
    vec3 V = normalize(u_CameraPosition - FragPos);
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, Albedo, Metallic);
    vec3 Lo = vec3(0.0);

    // 平行光计算
    vec3 L = normalize(-DirLightDir.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = DirLightColor.rgb;

    float NDF = DistributionGGX(N, H, Roughness);
    float G   = GeometrySmith(N, V, L, Roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - Metallic;     
    float NdotL = max(dot(N, L), 0.0);

    vec4 fragPosLightSpace = pc.u_LightSpaceMatrix * vec4(FragPos, 1.0);
    float shadow = ShadowCalculation(fragPosLightSpace, N, L);
    shadow *= ReceiveShadows;
    
    vec3 dirLightContribution = (kD * Albedo / PI + specular) * radiance * NdotL;
    Lo += dirLightContribution * (1.0 - shadow);

    // 点光源计算
    for(int i = 0; i < PointLightCount; ++i) {
        vec3 lightPos = PointLights[i].Position.xyz;
        float radius = PointLights[i].Position.w;
        vec3 lightColor = PointLights[i].Color.rgb;
        float falloff = PointLights[i].Color.w;
        
        vec3 L_point = normalize(lightPos - FragPos);
        vec3 H_point = normalize(V + L_point);
        float distance = length(lightPos - FragPos);
        
        float attenuation = 1.0 / (distance * distance + 0.0001);
        float distanceByRadius = distance / radius;
        float distanceByRadius4 = pow(distanceByRadius, 4.0);
        float windowing = clamp(1.0 - distanceByRadius4, 0.0, 1.0);
        windowing = pow(windowing, falloff + 1.0);
        attenuation *= windowing;
        
        vec3 radiance_point = lightColor * attenuation;
        
        float NDF_p = DistributionGGX(N, H_point, Roughness);   
        float G_p   = GeometrySmith(N, V, L_point, Roughness);
        vec3 F_p    = fresnelSchlick(max(dot(H_point, V), 0.0), F0);
        
        vec3 numerator_p    = NDF_p * G_p * F_p;
        float denominator_p = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L_point), 0.0) + 0.0001;
        vec3 specular_p     = numerator_p / denominator_p;

        vec3 kS_p = F_p;
        vec3 kD_p = vec3(1.0) - kS_p;
        kD_p *= 1.0 - Metallic;     
        float NdotL_p = max(dot(N, L_point), 0.0);
        
        Lo += (kD_p * Albedo / PI + specular_p) * radiance_point * NdotL_p;
    }

    // IBL 环境光
    vec3 F_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, Roughness);
    vec3 kS_ambient = F_ambient;
    vec3 kD_ambient = 1.0 - kS_ambient;
    kD_ambient *= 1.0 - Metallic; 
    
    vec3 irradiance = pc.u_AmbientColor;
    vec3 specular_ambient = vec3(0.0);

    if (pc.u_EnvMapEnabled == 1) {
        irradiance += texture(u_IrradianceMap, N).rgb * pc.u_Intensity;
        
        const float MAX_REFLECTION_LOD = 4.0; 
        vec3 R = reflect(-V, N);
        vec3 prefilteredColor = textureLod(u_PrefilteredMap, R, Roughness * MAX_REFLECTION_LOD).rgb * pc.u_Intensity;
        vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), Roughness)).rg;
        specular_ambient = prefilteredColor * (F_ambient * brdf.x + brdf.y);
    }
    
    vec3 diffuse = irradiance * Albedo;
    vec3 ambient = (kD_ambient * diffuse + specular_ambient) * AO;
    
    vec3 color = ambient + Lo;
    FragColor = vec4(color, 1.0);
}