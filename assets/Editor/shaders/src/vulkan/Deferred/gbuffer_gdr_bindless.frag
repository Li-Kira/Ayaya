#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec2 g_Normal;
layout(location = 1) out vec4 g_Albedo;
layout(location = 2) out vec4 g_PBR;
layout(location = 3) out vec4 g_CustomData;
layout(location = 4) out vec2 g_Velocity;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) flat in uint v_MaterialIdx;
layout(location = 4) flat in uint v_Flags;
layout(location = 5) in vec2 v_Velocity;

// set=1: bindless texture array
layout(set = 1, binding = 0) uniform sampler2D u_GlobalTextures[];

// set=2, binding=2: Material SSBO (shared with vertex shader)
// Fragment reads this directly — no push constants, no varying bandwidth waste
struct GPUMaterial {
    vec4 albedo;
    float metallic, roughness, ao, alpha;
    int useAlbedoMap, useNormalMap, useORMMap;
    int useMetallicMap, useRoughnessMap, useAOMap;
    int albedoBindless, normalBindless, ormBindless;
    int metallicBindless, roughnessBindless, aoBindless;
    float alphaCutoff;
    int blendMode;
    int useAlphaMap;
    int alphaBindless;
    	uint lightModeMask;
	uint packing; uint _pad1, _pad2;  // packing=TexturePacking enum, pads align to 16
	float customData[16];
};
layout(std430, set = 2, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial Materials[];
};

vec3 GetNormalFromMap(uint normalIdx) {
    vec3 tN = texture(u_GlobalTextures[nonuniformEXT(normalIdx)], v_TexCoord).xyz * 2.0 - 1.0;
    tN.g = -tN.g; // Vulkan negative-viewport compensation (dFdy inversion)
    vec3 Q1 = dFdx(v_FragPos), Q2 = dFdy(v_FragPos);
    vec2 st1 = dFdx(v_TexCoord), st2 = dFdy(v_TexCoord);
    vec3 N = normalize(v_Normal);
    vec3 T = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B = -normalize(cross(N, T));
    return normalize(mat3(T, B, N) * tN);
}

vec2 OctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = (n.z >= 0.0) ? n.xy : (vec2(1.0) - abs(n.yx)) * sign(n.xy);
    return e * 0.5 + 0.5;
}

void main() {
    GPUMaterial mat = Materials[v_MaterialIdx];

    vec4 albedoSample = texture(u_GlobalTextures[nonuniformEXT(mat.albedoBindless)], v_TexCoord);
    float a = mat.alpha * albedoSample.a;
    if (mat.useAlphaMap != 0)
        a *= texture(u_GlobalTextures[nonuniformEXT(mat.alphaBindless)], v_TexCoord).r;

    if (mat.blendMode == 1 && a < mat.alphaCutoff) discard;

    vec3 N = GetNormalFromMap(nonuniformEXT(mat.normalBindless));
    g_Normal = OctEncode(N);

    vec3 albedo = albedoSample.rgb * mat.albedo.rgb;
    g_Albedo = vec4(albedo, 1.0);

    float metallic, roughness, ao;

    if (mat.useORMMap != 0) {
        vec3 o = texture(u_GlobalTextures[nonuniformEXT(mat.ormBindless)], v_TexCoord).rgb;
        roughness = o.g * mat.roughness; metallic = o.b * mat.metallic;
        if (mat.packing == 1u) {
            // glTF_MetalRough: AO from separate occlusionTexture; R channel undefined in the ORM texture.
            // When no separate AO map exists, use scalar AO (1.0 = no occlusion).
            ao = (mat.aoBindless != 1)
                ? texture(u_GlobalTextures[nonuniformEXT(mat.aoBindless)], v_TexCoord).r * mat.ao
                : mat.ao;
        } else {
            // UE4_ORM (default): R=AO
            ao = o.r * mat.ao;
        }
    } else if (mat.metallicBindless != 1 || mat.roughnessBindless != 1 || mat.aoBindless != 1) {
        // Separate maps (legacy): texture overrides scalar (historical engine behavior).
        // When a texture map exists, the scalar factor is NOT multiplied — texture value
        // replaces scalar entirely. The scalar is only used as fallback when no map exists.
        metallic  = (mat.metallicBindless  != 1) ? texture(u_GlobalTextures[nonuniformEXT(mat.metallicBindless)],  v_TexCoord).r : mat.metallic;
        roughness = (mat.roughnessBindless != 1) ? texture(u_GlobalTextures[nonuniformEXT(mat.roughnessBindless)], v_TexCoord).r : mat.roughness;
        ao        = (mat.aoBindless        != 1) ? texture(u_GlobalTextures[nonuniformEXT(mat.aoBindless)],        v_TexCoord).r : mat.ao;
    } else {
        // Scalar-only (no texture maps at all)
        metallic = mat.metallic; roughness = mat.roughness; ao = mat.ao;
    }
    g_PBR = vec4(metallic, roughness, ao, 1.0);
    // .r = ReceiveShadows flag (read by deferred_lighting.frag as RcvShadow)
    float rcvShadow = ((v_Flags & 2u) != 0u) ? 1.0 : 0.0;
    g_CustomData = vec4(rcvShadow, 0.0, 0.0, 1.0);
    g_Velocity = v_Velocity;
}
