#version 450 core
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 2) uniform samplerCube u_PrefilterMap;
layout(set = 1, binding = 3) uniform sampler2D u_BRDFLUT;
layout(set = 1, binding = 4) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 5) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 6) uniform sampler2D u_AOMap;
layout(set = 1, binding = 7) uniform sampler2D u_NormalMap;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 ViewPos;
} u_Camera;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
    float Metallic;
    float Roughness;
    float AO;
    int UseMetallicMap;
    int UseRoughnessMap;
    int UseAOMap;
    int UseNormalMap;
} u_Push;

const float PI = 3.14159265359;

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 GetNormalFromMap(vec3 worldNormal, vec3 worldPos, vec2 texCoord) {
    vec3 tangentNormal = texture(u_NormalMap, texCoord).xyz * 2.0 - 1.0;
    vec3 Q1  = dFdx(worldPos);
    vec3 Q2  = dFdy(worldPos);
    vec2 st1 = dFdx(texCoord);
    vec2 st2 = dFdy(texCoord);
    vec3 N   = normalize(worldNormal);
    vec3 T   = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B   = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main() {
    vec3 albedo = u_Push.UseAlbedoMap == 1
        ? texture(u_AlbedoMap, v_TexCoord).rgb
        : u_Push.Albedo;

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
    vec3 V = normalize(u_Camera.ViewPos - v_WorldPos);
    vec3 R = reflect(-V, N);

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

    vec3 ambient = (kD * diffuse + specular) * ao;
    FragColor = vec4(ambient, 1.0);
}
