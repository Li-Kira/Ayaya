#version 450 core
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

// --- 贴图绑定 ---
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 2) uniform samplerCube u_PrefilterMap;
layout(set = 1, binding = 3) uniform sampler2D u_BRDFLUT;

// --- 相机 UBO ---
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 ViewPos;
} u_Camera;

const float PI = 3.14159265359;

// 菲涅尔近似 (F0 为基础反射率)
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 albedo = texture(u_AlbedoMap, v_TexCoord).rgb;
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_Camera.ViewPos - v_WorldPos);
    vec3 R = reflect(-V, N);

    // 假设固定材质属性，实际可从 Material UBO 传入
    float metallic = 0.5;
    float roughness = 0.2;

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // 计算菲涅尔项
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    // 1. 漫反射 IBL (Irradiance)
    vec3 irradiance = texture(u_IrradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    // 2. 镜面反射 IBL (Prefilter + LUT)
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    // 3. 最终组合
    vec3 ambient = (kD * diffuse + specular);
    
    FragColor = vec4(ambient, 1.0);
}