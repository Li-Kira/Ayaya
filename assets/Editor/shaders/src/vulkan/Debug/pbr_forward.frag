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

// 【核心修复】：必须与 pbr_forward.vert 完全一致，用来接收材质颜色
layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
} u_Push;

const float PI = 3.14159265359;

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 【核心修复】：智能判断是使用贴图还是纯色！
    vec3 albedo = u_Push.UseAlbedoMap == 1 ? texture(u_AlbedoMap, v_TexCoord).rgb : u_Push.Albedo;
    
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_Camera.ViewPos - v_WorldPos);
    vec3 R = reflect(-V, N);

    float metallic = 0.5;
    float roughness = 0.2;

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(u_IrradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular);
    FragColor = vec4(ambient, 1.0);
    // FragColor = vec4( 1.0);
}