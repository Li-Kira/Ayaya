#version 410 core
out vec4 FragColor;

in vec2 v_TexCoord;
in vec3 v_Normal;
in vec3 v_FragPos;
in mat3 v_TBN;

// PBR 材质参数
uniform vec3 u_Albedo;      // 基础颜色
uniform float u_Metallic;   // 金属度 (0 = 绝缘体/塑料, 1 = 纯金属)
uniform float u_Roughness;  // 粗糙度 (0 = 光滑镜面, 1 = 极度粗糙)
uniform float u_AO;         // 环境光遮蔽

// 新增贴图支持
uniform sampler2D u_AlbedoMap;
uniform bool u_UseAlbedoMap;

uniform sampler2D u_MetallicMap;
uniform bool u_UseMetallicMap;

uniform sampler2D u_RoughnessMap;
uniform bool u_UseRoughnessMap;

uniform sampler2D u_AOMap;
uniform bool u_UseAOMap;

// 光源与相机参数
layout(std140) uniform DirectionalLight {
    vec3 u_LightDir;
    vec3 u_LightColor;
};

layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPos;
};

uniform sampler2D u_NormalMap;
uniform bool u_UseNormalMap;

const float PI = 3.14159265359;

// ----------------------------------------------------------------------------
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 1. 漫反射颜色 (Albedo)
    vec3 albedo = u_Albedo;
    if (u_UseAlbedoMap) {
        albedo *= pow(texture(u_AlbedoMap, v_TexCoord).rgb, vec3(2.2));
    }

    // 2. 金属度 (Metallic)
    float metallic = u_Metallic;
    if (u_UseMetallicMap) {
        metallic *= texture(u_MetallicMap, v_TexCoord).r; // 通常读取 R 通道
    }

    // 3. 粗糙度 (Roughness)
    float roughness = u_Roughness;
    if (u_UseRoughnessMap) {
        roughness *= texture(u_RoughnessMap, v_TexCoord).r;
    }

    // 4. 环境光遮蔽 (AO)
    float ao = u_AO;
    if (u_UseAOMap) {
        ao *= texture(u_AOMap, v_TexCoord).r;
    }

    // ==========================================
    // 接下来使用全新的物理变量进行计算
    // ==========================================
    vec3 N = normalize(v_TBN[2]); 

    // 2. 如果开启了法线贴图，用贴图覆盖法线！
    if (u_UseNormalMap) {
        // 从贴图采样法线信息，默认是 [0, 1] 范围
        vec3 normalMap = texture(u_NormalMap, v_TexCoord).rgb;
        
        // 将颜色值 [0, 1] 映射到方向向量 [-1, 1]
        normalMap = normalMap * 2.0 - 1.0; 
        
        // 利用 TBN 矩阵，将切线空间的法线转换到世界空间！
        N = normalize(v_TBN * normalMap); 
    }

    vec3 V = normalize(u_CameraPos - v_FragPos);
    vec3 L = normalize(-u_LightDir);
    vec3 H = normalize(V + L); 

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic); // 注意这里用新的 albedo 和 metallic

    float NDF = DistributionGGX(N, H, roughness);   // 注意这里用 roughness
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);       

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    float NdotL = max(dot(N, L), 0.0);        
    vec3 Lo = (kD * albedo / PI + specular) * u_LightColor * NdotL;

    vec3 ambient = vec3(0.03) * albedo * ao; // 注意这里用 ao
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0)); 
    color = pow(color, vec3(1.0/2.2));   

    FragColor = vec4(color, 1.0);
}