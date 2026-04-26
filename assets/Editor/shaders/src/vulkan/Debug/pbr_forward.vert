#version 450 core
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 2) uniform samplerCube u_PrefilterMap;
layout(set = 1, binding = 3) uniform sampler2D u_BRDFLUT;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 ViewPos;
} u_Camera;

// 必须与 Vert 和 C++ 完全一致
layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
    vec3 LightDir;
    float Metallic;
    vec3 LightColor;
    float Roughness;
} u_Push;

const float PI = 3.14159265359;

// --- PBR 核心函数 ---
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness; float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0); float NdotH2 = NdotH*NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0); float k = (r*r) / 8.0;
    float num = NdotV; float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

void main() {
    vec3 albedo = u_Push.UseAlbedoMap == 1 ? texture(u_AlbedoMap, v_TexCoord).rgb : u_Push.Albedo;
    
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_Camera.ViewPos - v_WorldPos);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, u_Push.Metallic);

    // ==========================================
    // 1. 直接光照 (Analytical Light: 太阳光)
    // ==========================================
    vec3 L = normalize(-u_Push.LightDir);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, u_Push.Roughness);   
    float G   = GeometrySmith(N, V, L, u_Push.Roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);       

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular_dir = numerator / denominator;

    vec3 kS_dir = F;
    vec3 kD_dir = vec3(1.0) - kS_dir;
    kD_dir *= 1.0 - u_Push.Metallic;

    // 直接光照结果
    vec3 Lo = (kD_dir * albedo / PI + specular_dir) * u_Push.LightColor * NdotL;

    // ==========================================
    // 2. 间接光照 (IBL)
    // ==========================================
    vec3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, u_Push.Roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = 1.0 - kS_ibl;
    kD_ibl *= 1.0 - u_Push.Metallic;

    vec3 irradiance = texture(u_IrradianceMap, N).rgb;
    vec3 diffuse_ibl = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(u_PrefilterMap, R, u_Push.Roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), u_Push.Roughness)).rg;
    vec3 specular_ibl = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    vec3 ambient = (kD_ibl * diffuse_ibl + specular_ibl);

    // ==========================================
    // 3. 最终输出 (HDR，将在后处理中进行色调映射)
    // ==========================================
    vec3 finalColor = ambient + Lo;
    FragColor = vec4(finalColor, 1.0);
}