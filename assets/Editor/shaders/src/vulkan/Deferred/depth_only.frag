#version 450 core
// depth_only.frag — Depth pre-pass minimal fragment shader
// Used with ColorWriteMask=0 pipeline; only depth is written.
// Masked materials: discard pixels below alphaCutoff to prevent "cardboard cutout".

layout(location = 3) flat in uint v_MaterialIdx;  // matches gbuffer_gdr.vert location=3

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
    uint packing; uint _pad1, _pad2;
    float customData[16];
};
layout(std430, set = 2, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial Materials[];
};

layout(location = 0) out vec4 outDummy; // prevents compiler from optimizing away discard

void main() {
    GPUMaterial mat = Materials[v_MaterialIdx];
    if (mat.blendMode == 1) { // Masked
        if (mat.alpha < mat.alphaCutoff) discard;
    }
    outDummy = vec4(0.0); // ColorWriteMask=0 → GPU hardware intercepts, zero bandwidth
}
