#version 410 core
out vec4 FragColor;

in vec2 v_TexCoord;

// ==========================================
// 核心：读取 G-Buffer 的 4 张数据贴图
// ==========================================
uniform sampler2D g_Position;
uniform sampler2D g_Normal;
uniform sampler2D g_Albedo;
uniform sampler2D g_PBR; 

// ==========================================
// 引擎 UBO 接口
// ==========================================
layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

struct PointLight {
    vec4 Position;
    vec4 Color; // rgb * luminous power
};

layout(std140) uniform LightData {
    vec4 DirLightDir;   // xyz = dir, w = AmbientStrength
    vec4 DirLightColor; // xyz = color * illuminance
    PointLight PointLights[4];
    int PointLightCount;
};

// ==========================================
// PBR 核心数学函数 (Cook-Torrance BRDF)
// ==========================================
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

void main() {
    // 1. 从 G-Buffer 提取当前像素的物理数据
    vec4 posData = texture(g_Position, v_TexCoord);
    
    // 如果透明度通道为 0，说明这个像素没有画任何物体（是天空盒区域），直接丢弃光照计算！
    if (posData.a < 0.1) {
        discard;
    }

    vec3 FragPos = posData.rgb;
    vec3 Normal = texture(g_Normal, v_TexCoord).rgb;
    vec3 Albedo = texture(g_Albedo, v_TexCoord).rgb;
    
    vec4 pbrData = texture(g_PBR, v_TexCoord);
    float Metallic = pbrData.r;
    float Roughness = pbrData.g;
    float AO = pbrData.b;

    // 2. 基础 PBR 变量准备
    vec3 N = normalize(Normal);
    vec3 V = normalize(u_CameraPosition - FragPos);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, Albedo, Metallic);
    vec3 Lo = vec3(0.0);

    // ==========================================
    // 3. 计算平行光 (Directional Light)
    // ==========================================
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
    Lo += (kD * Albedo / PI + specular) * radiance * NdotL;

    // ==========================================
    // 4. 计算所有点光源 (Point Lights)
    // ==========================================
    for(int i = 0; i < PointLightCount; ++i) {
        vec3 lightPos = PointLights[i].Position.xyz;
        vec3 lightColor = PointLights[i].Color.rgb;
        
        vec3 L_point = normalize(lightPos - FragPos);
        vec3 H_point = normalize(V + L_point);
        
        float distance = length(lightPos - FragPos);
        float attenuation = 1.0 / (distance * distance);
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

    // ==========================================
    // 5. 环境光与最终合成
    // ==========================================
    float ambientStrength = DirLightDir.w; // 借用了方向光的 W 通道传环境光强度
    // vec3 ambient = vec3(0.03) * Albedo * AO * ambientStrength;
    vec3 ambient = vec3(ambientStrength) * Albedo * AO;
    
    vec3 color = ambient + Lo;

    // 输出到高动态范围 (HDR) 缓冲！
    FragColor = vec4(color, 1.0);
}