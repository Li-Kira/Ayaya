#version 410 core

// 对应 FBO 里的 4 个附件
layout(location = 0) out vec4 g_Position;
layout(location = 1) out vec4 g_Normal;
layout(location = 2) out vec4 g_Albedo;
layout(location = 3) out vec4 g_PBR; 

in vec3 v_FragPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

// 材质属性
uniform vec3 u_Albedo;
uniform bool u_UseAlbedoMap;
uniform sampler2D u_AlbedoMap;

uniform float u_Metallic;
uniform bool u_UseMetallicMap;
uniform sampler2D u_MetallicMap;

uniform float u_Roughness;
uniform bool u_UseRoughnessMap;
uniform sampler2D u_RoughnessMap;

uniform float u_AO;
uniform bool u_UseAOMap;
uniform sampler2D u_AOMap;

uniform bool u_UseNormalMap;
uniform sampler2D u_NormalMap;

// 从法线贴图中提取真实法线
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
    // 1. 存入世界坐标 (透明度通道填 1.0 表示这里有物体)
    g_Position = vec4(v_FragPos, 1.0);

    // 2. 存入法线
    vec3 finalNormal = u_UseNormalMap ? GetNormalFromMap() : normalize(v_Normal);
    g_Normal = vec4(finalNormal, 1.0);

    // 3. 存入颜色
    vec3 albedo = u_UseAlbedoMap ? pow(texture(u_AlbedoMap, v_TexCoord).rgb, vec3(2.2)) : u_Albedo;
    g_Albedo = vec4(albedo, 1.0);
    float metallic = u_UseMetallicMap ? texture(u_MetallicMap, v_TexCoord).r : u_Metallic;
    float roughness = u_UseRoughnessMap ? texture(u_RoughnessMap, v_TexCoord).r : u_Roughness;
    float ao = u_UseAOMap ? texture(u_AOMap, v_TexCoord).r : u_AO;
    
    g_PBR = vec4(metallic, roughness, ao, 1.0);
}