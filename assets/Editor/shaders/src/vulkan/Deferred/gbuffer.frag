#version 450 core

layout(location = 0) out vec4 g_Normal;
layout(location = 1) out vec4 g_Albedo;
layout(location = 2) out vec4 g_PBR;
layout(location = 3) out vec4 g_CustomData;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(set = 1, binding = 1) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 2) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 3) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D u_AOMap;
layout(set = 1, binding = 5) uniform sampler2D u_NormalMap;
layout(set = 1, binding = 6) uniform sampler2D u_AlphaMap;
layout(set = 1, binding = 7) uniform sampler2D u_ORMMap;

layout(push_constant) uniform PushConstants {
    mat4 u_Transform;
    vec3 u_Albedo; float u_ReceiveShadows;
    float u_Metallic; float u_Roughness; float u_AO;
    float u_AlphaMultiplier; float u_AlphaCutoff;
    int u_BlendMode;
    int u_UseAlbedoMap; int u_UseNormalMap; int u_UseORMMap;
    int u_UseMetallicMap; int u_UseRoughnessMap; int u_UseAOMap; int u_UseAlphaMap;
    int u_IsSelected;
} pc;

vec3 GetNormalFromMap() {
    vec3 tN = texture(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;
    vec3 Q1 = dFdx(v_FragPos), Q2 = dFdy(v_FragPos);
    vec2 st1 = dFdx(v_TexCoord), st2 = dFdy(v_TexCoord);
    vec3 N = normalize(v_Normal);
    vec3 T = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B = -normalize(cross(N, T));
    return normalize(mat3(T, B, N) * tN);
}

void main() {
    float a = pc.u_AlphaMultiplier;
    if (pc.u_UseAlbedoMap == 1) a *= texture(u_AlbedoMap, v_TexCoord).a;
    else if (pc.u_UseAlphaMap == 1) a *= texture(u_AlphaMap, v_TexCoord).r;
    if (pc.u_BlendMode == 1 && a < pc.u_AlphaCutoff) discard;

    vec3 N = (pc.u_UseNormalMap == 1) ? GetNormalFromMap() : normalize(v_Normal);
    g_Normal = vec4(N, 1.0);

    vec3 albedo = (pc.u_UseAlbedoMap == 1) ? texture(u_AlbedoMap, v_TexCoord).rgb : pc.u_Albedo;
    g_Albedo = vec4(albedo, 1.0);

    float metallic, roughness, ao;
    if (pc.u_UseORMMap == 1) { vec3 o = texture(u_ORMMap, v_TexCoord).rgb; ao=o.r; roughness=o.g; metallic=o.b; }
    else { metallic=pc.u_Metallic; roughness=pc.u_Roughness; ao=pc.u_AO;
        if(pc.u_UseMetallicMap==1) metallic=texture(u_MetallicMap,v_TexCoord).r;
        if(pc.u_UseRoughnessMap==1) roughness=texture(u_RoughnessMap,v_TexCoord).r;
        if(pc.u_UseAOMap==1) ao=texture(u_AOMap,v_TexCoord).r; }
    g_PBR = vec4(metallic, roughness, ao, 1.0);
    g_CustomData = vec4(pc.u_ReceiveShadows, float(pc.u_IsSelected), 0.0, 1.0);
}