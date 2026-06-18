#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec2 g_Normal;  // octahedral-encoded world-space normal
layout(location = 1) out vec4 g_Albedo;
layout(location = 2) out vec4 g_PBR;
layout(location = 3) out vec4 g_CustomData;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

// Bindless texture array — all material 2D textures indexed via push constants
layout(set = 1, binding = 0) uniform sampler2D u_GlobalTextures[];

layout(push_constant) uniform PushConstants {
    mat4   u_Transform;                        // offset 0   (64B)
    vec4   u_Albedo_ReceiveShadows;            // offset 64  (16B)  xyz=Albedo, w=ReceiveShadows
    vec4   u_Metallic_Roughness_AO_Alpha;      // offset 80  (16B)  x=Metallic, y=Roughness, z=AO, w=Alpha
    vec4   u_AlphaCutoff_BlendMode_UseORMMap;  // offset 96  (16B)  x=AlphaCutoff, y=BlendMode, z=UseORMMap
    uvec4  u_Indices0;   // x=AlbedoMap, y=NormalMap, z=ORMMap, w=MetallicMap   (16B) @112
    uvec4  u_Indices1;   // x=RoughnessMap, y=AOMap, z=AlphaMap, w=IsSelected    (16B) @128
} pc;

vec3 GetNormalFromMap(uint normalIdx) {
    vec3 tN = texture(u_GlobalTextures[nonuniformEXT(normalIdx)], v_TexCoord).xyz * 2.0 - 1.0;
    vec3 Q1 = dFdx(v_FragPos), Q2 = dFdy(v_FragPos);
    vec2 st1 = dFdx(v_TexCoord), st2 = dFdy(v_TexCoord);
    vec3 N = normalize(v_Normal);
    vec3 T = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B = -normalize(cross(N, T));
    return normalize(mat3(T, B, N) * tN);
}

// Octahedral encode: unit vec3 → 2-component in [0,1] for RG16F storage
vec2 OctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = (n.z >= 0.0) ? n.xy : (vec2(1.0) - abs(n.yx)) * sign(n.xy);
    return e * 0.5 + 0.5;
}

void main() {
    // Albedo sampling — index always valid (defaults to white 1×1)
    vec4 albedoSample = texture(u_GlobalTextures[nonuniformEXT(pc.u_Indices0.x)], v_TexCoord);
    float a = pc.u_Metallic_Roughness_AO_Alpha.w * albedoSample.a;

    if (int(pc.u_AlphaCutoff_BlendMode_UseORMMap.y) == 1 && a < pc.u_AlphaCutoff_BlendMode_UseORMMap.x)
        discard;

    // Normal — always sampled from bindless array (default = flat Z-up normal map)
    vec3 N = GetNormalFromMap(pc.u_Indices0.y);
    g_Normal = OctEncode(N);

    vec3 albedo = albedoSample.rgb * pc.u_Albedo_ReceiveShadows.rgb;
    g_Albedo = vec4(albedo, 1.0);

    float metallic, roughness, ao;
    if (pc.u_AlphaCutoff_BlendMode_UseORMMap.z != 0u) {
        // ORM packed: R=AO, G=Roughness, B=Metallic
        vec3 o = texture(u_GlobalTextures[nonuniformEXT(pc.u_Indices0.z)], v_TexCoord).rgb;
        ao = o.r; roughness = o.g; metallic = o.b;
    } else {
        // Individual maps: scalar * map (default index 1 = white, so unused maps give scalar * 1 = scalar)
        metallic  = pc.u_Metallic_Roughness_AO_Alpha.x * texture(u_GlobalTextures[nonuniformEXT(pc.u_Indices0.w)], v_TexCoord).r;
        roughness = pc.u_Metallic_Roughness_AO_Alpha.y * texture(u_GlobalTextures[nonuniformEXT(pc.u_Indices1.x)], v_TexCoord).r;
        ao        = pc.u_Metallic_Roughness_AO_Alpha.z * texture(u_GlobalTextures[nonuniformEXT(pc.u_Indices1.y)], v_TexCoord).r;
    }
    g_PBR = vec4(metallic, roughness, ao, 1.0);
    g_CustomData = vec4(pc.u_Albedo_ReceiveShadows.w, pc.u_Indices1.w, 0.0, 1.0);
}
