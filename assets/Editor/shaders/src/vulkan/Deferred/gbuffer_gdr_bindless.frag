#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec2 g_Normal;
layout(location = 1) out vec4 g_Albedo;
layout(location = 2) out vec4 g_PBR;
layout(location = 3) out vec4 g_CustomData;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) flat in uint v_MaterialIdx;

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
    int _pad[2];
};
layout(std430, set = 2, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial Materials[];
};

vec3 GetNormalFromMap(uint normalIdx) {
    vec3 tN = texture(u_GlobalTextures[nonuniformEXT(normalIdx)], v_TexCoord).xyz * 2.0 - 1.0;
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
        ao = o.r; roughness = o.g; metallic = o.b;
    } else {
        // Mirror CPU-side GetRenderScalars(): when a texture map is present
        // (bindless index ≠ white default=1), override scalar to 1.0 so
        // shader produces 1.0 * texture = texture — not scalar * texture.
        float m = (mat.metallicBindless  != 1) ? 1.0 : mat.metallic;
        float r = (mat.roughnessBindless != 1) ? 1.0 : mat.roughness;
        float a = (mat.aoBindless        != 1) ? 1.0 : mat.ao;
        metallic  = m * texture(u_GlobalTextures[nonuniformEXT(mat.metallicBindless)],  v_TexCoord).r;
        roughness = r * texture(u_GlobalTextures[nonuniformEXT(mat.roughnessBindless)], v_TexCoord).r;
        ao        = a * texture(u_GlobalTextures[nonuniformEXT(mat.aoBindless)],        v_TexCoord).r;
    }
    g_PBR = vec4(metallic, roughness, ao, 1.0);
    g_CustomData = vec4(1.0, 0.0, 0.0, 1.0);
}
