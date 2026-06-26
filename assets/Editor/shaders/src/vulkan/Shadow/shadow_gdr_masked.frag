#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

// Alpha-tested shadow fragment shader (Masked materials only)
// Samples bindless albedo texture → discards fragments below alphaCutoff
// Depth is written automatically; no color attachment output

// set=1: bindless texture array
layout(set = 1, binding = 0) uniform sampler2D u_GlobalTextures[];

// set=2, binding=2: Material SSBO (shared with vertex shader)
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
	uint _pad0, _pad1, _pad2;  // align customData to 16-byte boundary (match C++ alignas(16))
	float customData[16];
};
layout(std430, set = 2, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial Materials[];
};

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) flat in uint v_MaterialIdx;

void main() {
    GPUMaterial mat = Materials[v_MaterialIdx];

    float alpha = mat.alpha * texture(u_GlobalTextures[nonuniformEXT(mat.albedoBindless)], v_TexCoord).a;
    if (mat.useAlphaMap != 0)
        alpha *= texture(u_GlobalTextures[nonuniformEXT(mat.alphaBindless)], v_TexCoord).r;

    if (alpha < mat.alphaCutoff) {
        discard;
    }
    // No color output — depth-only (depth written automatically)
}
