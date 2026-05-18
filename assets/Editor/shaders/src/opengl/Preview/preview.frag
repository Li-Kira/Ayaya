#version 410 core

layout(location = 0) out vec4 FragColor;

in vec3 v_Normal;
in vec3 v_WorldPos;

uniform vec3 u_CameraPos;
uniform vec3 u_Albedo;

// ACES Filmic 电影级色调映射曲线
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_CameraPos - v_WorldPos);
    float NdotV = max(dot(N, V), 0.0001);

    // 1. 经典 Clay (粘土/塑料) 材质基础
    vec3 albedo = u_Albedo;
    float roughness = 0.38;

    // 2. 影棚三点式布光 (Studio Lighting Rig)
    vec3 keyLightDir   = normalize(vec3(0.8, 1.0, 0.6));
    vec3 keyLightColor = vec3(1.0, 0.96, 0.9) * 2.2;

    vec3 fillLightDir   = normalize(vec3(-0.8, 0.4, -0.4));
    vec3 fillLightColor = vec3(0.5, 0.6, 0.8) * 0.8;

    vec3 backLightDir   = normalize(vec3(0.0, 0.2, -1.0));
    vec3 backLightColor = vec3(0.9, 0.95, 1.0) * 1.8;

    // 3. 半球环境光
    vec3 groundColor = vec3(0.04, 0.04, 0.05);
    vec3 skyColor    = vec3(0.18, 0.20, 0.25);
    vec3 ambient     = mix(groundColor, skyColor, N.y * 0.5 + 0.5) * 0.8;

    // 4. 光照计算核心
    vec3 finalColor = ambient * albedo;

    // --- 主光 (Diffuse + Specular) ---
    vec3 H_key = normalize(keyLightDir + V);
    float NdotL_key = max(dot(N, keyLightDir), 0.0);
    float NdotH_key = max(dot(N, H_key), 0.0);
    float spec_key = pow(NdotH_key, mix(2.0, 2048.0, 1.0 - roughness)) * (1.0 - roughness);
    finalColor += (albedo + vec3(spec_key)) * keyLightColor * NdotL_key;

    // --- 辅光 (纯 Diffuse) ---
    float NdotL_fill = max(dot(N, fillLightDir), 0.0);
    finalColor += albedo * fillLightColor * NdotL_fill;

    // --- 边缘背光 (Rim + Fresnel) ---
    float NdotL_back = max(dot(N, backLightDir), 0.0);
    float rimFactor = pow(1.0 - NdotV, 3.5);
    finalColor += albedo * backLightColor * NdotL_back * rimFactor;

    // 5. 后处理：ACES 色调映射 + sRGB Gamma
    finalColor = ACESFilm(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    FragColor = vec4(finalColor, 1.0);
}
