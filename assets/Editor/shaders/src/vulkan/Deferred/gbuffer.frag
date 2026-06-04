#version 450 core

layout(location = 0) out vec4 g_Position;
layout(location = 1) out vec4 g_Normal;
layout(location = 2) out vec4 g_Albedo;
layout(location = 3) out vec4 g_PBR;
layout(location = 4) out vec4 g_CustomData;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(set = 1, binding = 1) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 2) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 3) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D u_AOMap;
layout(set = 1, binding = 5) uniform sampler2D u_NormalMap;

layout(push_constant) uniform PushConstants {
    mat4 u_Transform;
    vec3 u_Albedo;
    float u_ReceiveShadows;
    float u_Metallic;
    float u_Roughness;
    float u_AO;
    int u_UseAlbedoMap;
    int u_UseMetallicMap;
    int u_UseRoughnessMap;
    int u_UseAOMap;
    int u_UseNormalMap;
} pc;

vec3 GetNormalFromMap() {
    vec3 tangentNormal = texture(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;
    vec3 Q1  = dFdx(v_FragPos);
    vec3 Q2  = dFdy(v_FragPos);
    vec2 st1 = dFdx(v_TexCoord);
    vec2 st2 = dFdy(v_TexCoord);
    vec3 N   = normalize(v_Normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main() {
    g_Position = vec4(v_FragPos, 1.0);

    vec3 finalNormal = (pc.u_UseNormalMap == 1) ? GetNormalFromMap() : normalize(v_Normal);
    g_Normal = vec4(finalNormal, 1.0);

    vec3 albedo = (pc.u_UseAlbedoMap == 1) ? texture(u_AlbedoMap, v_TexCoord).rgb : pc.u_Albedo;
    g_Albedo = vec4(albedo, 1.0);

    float metallic  = (pc.u_UseMetallicMap  == 1) ? texture(u_MetallicMap,  v_TexCoord).r : pc.u_Metallic;
    float roughness = (pc.u_UseRoughnessMap == 1) ? texture(u_RoughnessMap, v_TexCoord).r : pc.u_Roughness;
    float ao        = (pc.u_UseAOMap       == 1) ? texture(u_AOMap,        v_TexCoord).r : pc.u_AO;
    g_PBR = vec4(metallic, roughness, ao, 1.0);

    g_CustomData = vec4(pc.u_ReceiveShadows, 0.0, 0.0, 1.0);
}
