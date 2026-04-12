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
uniform sampler2D g_CustomData;

// 【新增】：IBL 专属采样器
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilteredMap;
uniform sampler2D   u_BRDFLUT;
uniform bool        u_EnvMapEnabled;
uniform float       u_Intensity;
uniform vec3        u_AmbientColor;

// 阴影
uniform sampler2D u_ShadowMap;       // 我们等会传进来的影子贴图 (挂在槽位 7)
uniform mat4 u_LightSpaceMatrix;     // 太阳视角的矩阵

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
    vec4 DirLightDir;   // xyz = dir, w = padding (已废弃的 AmbientStrength)
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

// 菲涅尔方程 (考虑粗糙度)
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 带PCF的阴影算法
// 带PCF的阴影算法
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // 执行透视除法 (将坐标归一化到 -1 ~ 1)
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // 变换到 0 ~ 1 范围，方便去纹理里采样
    projCoords = projCoords * 0.5 + 0.5;
    
    // 如果超出了阴影视锥体的范围，默认没有阴影
    if(projCoords.z > 1.0) return 0.0;

    // 当前像素在太阳眼里的深度
    float currentDepth = projCoords.z;
    
    // ==========================================
    // 【核心修复】：稍微加大 Bias 的基础值！
    // 因为我们关闭了正面剔除，需要稍强的偏移来抵抗斑马纹
    // 原来是 0.0015 和 0.0002，现在提升到 0.005 和 0.0005
    // ==========================================
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    
    // PCF (Percentage-Closer Filtering) 软阴影：对周围 9 个像素进行采样求平均值
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
    // 1. 从 G-Buffer 提取当前像素的物理数据
    vec4 posData = texture(g_Position, v_TexCoord);
    // 如果透明度通道为 0，说明这个像素没有画任何物体（是天空盒区域），直接丢弃光照计算！
    if (posData.a < 0.1) {
        discard;
    }

    vec3 FragPos = posData.rgb;
    vec3 Normal = texture(g_Normal, v_TexCoord).rgb;
    vec4 albedoData = texture(g_Albedo, v_TexCoord);
    vec3 Albedo = albedoData.rgb;
    
    vec4 pbrData = texture(g_PBR, v_TexCoord);
    float Metallic = pbrData.r;
    float Roughness = pbrData.g;
    float AO = pbrData.b;

    vec4 customData = texture(g_CustomData, v_TexCoord);
    float ReceiveShadows = customData.r;

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

    // 1. 将当前像素的坐标转化为太阳眼里的坐标 (复用已有的 FragPos 即可，比额外采样 G-Buffer 更快)
    vec4 fragPosLightSpace = u_LightSpaceMatrix * vec4(FragPos, 1.0);
    // 2. 查字典，计算阴影遮蔽率 (0.0 是全亮，1.0 是死黑)
    float shadow = ShadowCalculation(fragPosLightSpace, N, L);
    // 3. 算出太阳光贡献
    // 【终极绝杀】：乘以接收阴影的标记！
    // 如果该物体 ReceiveShadows == 0.0，那么 shadow 强制归零，光照完全不受影响！
    shadow *= ReceiveShadows; 
    vec3 dirLightContribution = (kD * Albedo / PI + specular) * radiance * NdotL;
    // 4. 乘上 (1.0 - shadow)！只有未被遮挡的光才能照亮像素！
    Lo += dirLightContribution * (1.0 - shadow);
    
    // ==========================================
    // 4. 计算所有点光源 (Point Lights)
    // ==========================================
    for(int i = 0; i < PointLightCount; ++i) {
        vec3 lightPos = PointLights[i].Position.xyz;
        float radius = PointLights[i].Position.w; // 提取 Radius
        
        vec3 lightColor = PointLights[i].Color.rgb;
        float falloff = PointLights[i].Color.w;   // 提取 Falloff
        
        vec3 L_point = normalize(lightPos - FragPos);
        vec3 H_point = normalize(V + L_point);
        float distance = length(lightPos - FragPos);
        
        // 物理平方反比衰减 (加极小值防止除以0爆炸)
        float attenuation = 1.0 / (distance * distance + 0.0001);
        
        // 【核心魔法】：Unreal Engine 4 窗函数衰减 (Windowing Falloff)
        // 保证光线在到达 Radius 时，非常平滑且物理正确地降为 0
        float distanceByRadius = distance / radius;
        float distanceByRadius4 = pow(distanceByRadius, 4.0);
        float windowing = clamp(1.0 - distanceByRadius4, 0.0, 1.0);
        windowing = pow(windowing, falloff + 1.0); // 利用 Falloff 微调边缘柔和度
        
        attenuation *= windowing; // 最终衰减 = 物理衰减 * 截断窗函数
        
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
    // 5. 终极 IBL 物理环境光合成 (Split-Sum Approximation)
    // ==========================================
    vec3 F_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, Roughness);
    vec3 kS_ambient = F_ambient;
    vec3 kD_ambient = 1.0 - kS_ambient;
    kD_ambient *= 1.0 - Metallic; 
    
    // (Part 1): Diffuse 漫反射
    vec3 irradiance = u_AmbientColor;
    vec3 specular_ambient = vec3(0.0);

    // 【核心修复】：只有当环境贴图存在时，才去采样天空盒！
    if (u_EnvMapEnabled) {
        irradiance += texture(u_IrradianceMap, N).rgb * u_Intensity;
        
        const float MAX_REFLECTION_LOD = 4.0; 
        vec3 R = reflect(-V, N);
        vec3 prefilteredColor = textureLod(u_PrefilteredMap, R, Roughness * MAX_REFLECTION_LOD).rgb * u_Intensity;
        vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), Roughness)).rg;
        specular_ambient = prefilteredColor * (F_ambient * brdf.x + brdf.y);
    }
    vec3 diffuse    = irradiance * Albedo;
    vec3 ambient = (kD_ambient * diffuse + specular_ambient) * AO;
    
    vec3 color = ambient + Lo;
    FragColor = vec4(color, 1.0);

    // FragColor = vec4(texture(u_BRDFLUT, v_TexCoord).rg, 0.0, 1.0);
    // FragColor = vec4(textureLod(u_PrefilteredMap, reflect(-V, N), Roughness * 4.0).rgb, 1.0); // 除以强度还原颜色
}