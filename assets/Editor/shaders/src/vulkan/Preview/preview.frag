#version 450 core

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 CameraPosition;
} u_Camera;

layout(push_constant) uniform PushData {
    mat4 ModelMatrix;
    vec4 Albedo;
    vec4 LightDir;
    vec4 LightColor;
    vec4 Ambient;
    int UseAlbedoMap;
} u_Push;

vec3 ACESFilm(vec3 x) {
    float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_Camera.CameraPosition - v_WorldPos);
    float NdotV = max(dot(N, V), 0.0001);

    vec3 albedo = u_Push.Albedo.rgb;
    float roughness = 0.38;

    // 2. 影棚三点式布光 (Studio Lighting Rig)
    vec3 keyLightDir   = normalize(vec3(0.8, 1.0, 0.6));
    vec3 keyLightColor = vec3(1.0, 0.96, 0.9) * 2.2;     // 主光：暖色阳光，强度高

    vec3 fillLightDir   = normalize(vec3(-0.8, 0.4, -0.4));
    vec3 fillLightColor = vec3(0.5, 0.6, 0.8) * 0.8;     // 辅光：冷色天光反弹，照亮暗部

    vec3 backLightDir   = normalize(vec3(0.0, 0.2, -1.0));
    vec3 backLightColor = vec3(0.9, 0.95, 1.0) * 1.8;    // 背光：强烈的冷白光，用于勾勒边缘

    // 3. 半球环境光 (Hemispherical Ambient - 模拟上下渐变的漫反射环境)
    vec3 groundColor = vec3(0.04, 0.04, 0.05);
    vec3 skyColor    = vec3(0.18, 0.20, 0.25);
    vec3 ambient     = mix(groundColor, skyColor, N.y * 0.5 + 0.5) * 0.8;

    // 4. 光照计算核心
    vec3 finalColor = ambient * albedo;

    // --- 主光计算 (Diffuse + 改进的 Specular) ---
    vec3 H_key = normalize(keyLightDir + V);
    float NdotL_key = max(dot(N, keyLightDir), 0.0);
    float NdotH_key = max(dot(N, H_key), 0.0);
    // 模拟 GGX 的大体视觉感
    float spec_key = pow(NdotH_key, mix(2.0, 2048.0, 1.0 - roughness)) * (1.0 - roughness);
    finalColor += (albedo + vec3(spec_key)) * keyLightColor * NdotL_key;

    // --- 辅光计算 (纯 Diffuse，柔和) ---
    float NdotL_fill = max(dot(N, fillLightDir), 0.0);
    finalColor += albedo * fillLightColor * NdotL_fill;

    // --- 边缘背光计算 (Rim Light 结合 Fresnel) ---
    float NdotL_back = max(dot(N, backLightDir), 0.0);
    float rimFactor = pow(1.0 - NdotV, 3.5); // 视角越边缘，背光越强烈
    finalColor += albedo * backLightColor * NdotL_back * rimFactor;

    // 5. 后处理：ACES 色调映射 + sRGB Gamma 矫正
    finalColor = ACESFilm(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    FragColor = vec4(finalColor, 1.0);
}
